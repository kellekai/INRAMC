# INRAMC
in memory ckpt prototype
# test on Marconi supercomputer
```
< Summary >
========================================
processes       : 2560
app processes   : 2400
head processes  : 160
GB per process  : 0.666667
GB total        : 1599.999994
========================================

1510760259.363272 : [IN MEM CKPT] [APP#1] ckpt in mem of 1599.999994 GB took: 3.944182 seconds
1510760283.463848 : [FLUSH PFS] [HEAD#0] flushing 1599.999994 GB to PFS took: 27.934567 seconds

```
# test on Mare Nostrum 4 supercomputer

```
< Summary >
========================================
processes       : 2400
app processes   : 1800
head processes  : 600
GB per process  : 1.111111
GB total        : 1999.999988
========================================

1510779845.183272 : [IN MEM CKPT] [APP#1] ckpt in mem of 1999.999988 GB took: 1.386995 seconds
1510779957.503907 : [FLUSH SSD] [HEAD#0] flushing 1999.999988 GB to PFS took: 113.697267 seconds

```
