#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _MAX_MEM_SIZE_GB 80
#define _GB_UNIT 1073741824

int main(int argc, char *argv[]) {
    
    /*
    * init MPI and declare/init variables
    */
    
    int heads = ( argc > 1 ) ? atoi(argv[1]) : 1;
    int thread_policy;
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &thread_policy);
    MPI_Comm ncomm = MPI_COMM_WORLD;
    MPI_Comm shmcomm; // shared memory comm
    MPI_Comm kcomm; // head group comm (contains head and corresponding app procs)
    MPI_Comm ksubcomm; // contains only app procs in head group 
    MPI_Comm kindcomm; // contains all app/head procs respectively
    MPI_Group ngroup, kgroup;
    int i, krank, ksize, ksubrank, ksubsize, kindrank, kindsize, shmrank, shmsize, nrank, nsize;
    // MPI_WORLD_COMM
    MPI_Comm_rank(ncomm, &nrank);
    MPI_Comm_size(ncomm, &nsize);
    MPI_Comm_group(ncomm, &ngroup); // needed to translate ranks from kcomm -> ncomm
    // SHM COMM (procs that share shm region)
    MPI_Comm_split_type(ncomm, MPI_COMM_TYPE_SHARED, 0, MPI_INFO_NULL, &shmcomm);
    MPI_Comm_rank(shmcomm, &shmrank);
    MPI_Comm_size(shmcomm, &shmsize);
    int head = (shmrank%(shmsize/heads) == 0) ? 1 : 0; // prockind = 1 for head
    // K COMM (procs of the k'th head, includes head)
    MPI_Comm_split(shmcomm, shmrank/(shmsize/heads), 0, &kcomm);
    MPI_Comm_rank(kcomm, &krank);
    MPI_Comm_size(kcomm, &ksize);
    MPI_Comm_group(kcomm, &kgroup); // needed to translate ranks from kcomm -> ncomm
    // K SUB COMM (splits kcomm into head and app procs)
    MPI_Comm_split(kcomm, head, 0, &ksubcomm);
    MPI_Comm_rank(ksubcomm, &ksubrank);
    MPI_Comm_size(ksubcomm, &ksubsize);
    // KIND COMM all app/head procs
    MPI_Comm_split(ncomm, head, 0, &kindcomm);
    MPI_Comm_rank(kindcomm, &kindrank);
    MPI_Comm_size(kindcomm, &kindsize);
    
    double start_t, total_t;
    
    int datadisp = sizeof(double);
    long MAX_MEM_SIZE_GB = _MAX_MEM_SIZE_GB; 
    long GB_UNIT = _GB_UNIT;
    unsigned long dataelem = ((unsigned long)((((double)(MAX_MEM_SIZE_GB*GB_UNIT))/(shmsize-heads))/2.0))/sizeof(double); // determine # of elements
    unsigned long datasize = dataelem*sizeof(double); // determine datasize in Bytes
    
    // RMA window definition, non-contiguous
    MPI_Win win;
    MPI_Info win_info;
    MPI_Info_create(&win_info);
    MPI_Info_set(win_info, "alloc_shared_noncontig", "true");

    double *shmdata = NULL;

    /*
     * app procs
     */

    if(nrank == 0) {

        printf("\n< Summary >\n"
                "========================================\n"
                "processes       : %i\n"
                "app processes   : %i\n"
                "head processes  : %i\n"
                "GB per process  : %lf\n"
                "GB total        : %lf\n"
                "========================================\n\n",
                nsize, 
                nsize - (nsize/shmsize)*heads, 
                (nsize/shmsize)*heads, 
                ((double)datasize)/GB_UNIT, 
                (((double)datasize)/GB_UNIT)*(nsize - (nsize/shmsize)*heads));

    }

    if(!head) {

        // initialize app data to some value
        double *data = (double*) malloc(datasize);
        memset(data,0x34,datasize);
        
        /*
         * create shared memory window
         */
        
        MPI_Barrier(kindcomm);
        start_t = MPI_Wtime();
        MPI_Win_allocate_shared(datasize, datadisp, win_info, kcomm, &shmdata, &win);
        MPI_Info_free(&win_info);
        
        /*
         * copy app data to shm buffer (checkpoint)
         */
        
        // query pointer to allocation
        MPI_Win_shared_query(win, krank, &datasize, &datadisp, &shmdata);
        memcpy(shmdata,data,datasize);
        MPI_Barrier(ksubcomm);
        total_t = MPI_Wtime()-start_t;
        
        /*
         * print duration
         */
        
        //if(ksubrank==0) {
        //    printf("%lf : [PART] [APP#%i] Initialization of %lf GB took: %lf seconds\n",
        //            nrank, MPI_Wtime(), ksubsize*((double)(datasize)/(1024*1024*1024)), total_t);
        //}
        
        MPI_Barrier(kindcomm);
        total_t = MPI_Wtime()-start_t;
        
        if(kindrank==0) {
            printf("%lf : [IN MEM CKPT] [APP#%i] ckpt in mem of %lf GB took: %lf seconds\n",
                    nrank, MPI_Wtime(), kindsize*((double)(datasize)/(1024*1024*1024)), total_t);
        }
        
        free(data);
    
    } else {
        
        FILE *ckptf; // pfs file pointer
        char ckptfn[256]; // file name
        int *nranks = (int*) malloc(sizeof(int)*shmsize);
        int *kranks = (int*) malloc(sizeof(int)*shmsize);
        unsigned long written = 0;
        char *ptr;
        for(i=0;i<ksize;i++) { 
            kranks[i] = i;
        }
        
        /*
         * create shared memory window
         */
        MPI_Win_allocate_shared(0, datadisp, win_info, kcomm, &shmdata, &win);
        start_t = MPI_Wtime();
        MPI_Info_free(&win_info);
        
        /*
         * get global ranks (ngroup -> ncomm)
         */

        MPI_Group_translate_ranks(kgroup, ksize, kranks, ngroup, nranks);
        
        /*
         * write checkpoint to PFS (using hyper-threading)
         */

#pragma omp parallel 
#pragma omp single
#pragma omp taskloop grainsize(1) private(ckptfn, ckptf, ptr, written, i, datasize, datadisp, shmdata)
        for(i=1; i<ksize; i++) {
            sprintf(ckptfn, "/marconi_scratch/userexternal/kkeller0/aqui-%i.ckptfile", nranks[i]);
            ckptf = fopen(ckptfn, "wb");
            MPI_Win_shared_query(win, i, &datasize, &datadisp, &shmdata);
            ptr = (char*) shmdata;
            while (written < datasize/datadisp) {
                written += fwrite(ptr, datadisp, datasize/datadisp - written, ckptf);
                ptr += written;
            }
            fclose(ckptf);
            written = 0;
        }
        //total_t = MPI_Wtime() - start_t;
        //printf("%lf : [PART] [HEAD#%i] writing %lf GB to PFS took: %lf seconds\n",
        //        nrank, MPI_Wtime(), (shmsize/heads-1)*((double)(datasize)/(1024*1024*1024)), total_t);
        //
        MPI_Barrier(kindcomm);
        total_t = MPI_Wtime() - start_t;
        if (kindrank == 0) {
            printf("%lf : [FLUSH PFS] [HEAD#%i] flushing %lf GB to PFS took: %lf seconds\n",
                    nrank, MPI_Wtime(), (nsize-(nsize/shmsize)*heads)*((double)(datasize)/(1024*1024*1024)), total_t);
        }
    }
    MPI_Win_free(&win);
    MPI_Barrier(ncomm);
    printf("\n");
    MPI_Finalize();
    return 0;
}

