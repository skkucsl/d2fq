# D2FQ
[D2FQ](https://github.com/skkucsl/d2fq) is a fair-queueing I/O scheduler using the NVMe WRR queue arbitration feature. Different from other conventional fair-queueing block I/O schedulers, D2FQ does not stage I/O requests in the block layer but dispatches them immediately to an SSD. Consequently, D2FQ achieves low I/O latency as well as low CPU consumption. 

This repository contains the implementation of D2FQ in the Linux kernel. We will open the source code as soon as possible. Please stay tuned. 

Please cite the following paper if you use D2FQ:

**D2FQ: Device-Direct Fair Queueing for NVMe SSDs**.
Jiwon Woo, Minwoo Ahn, Gyusun Lee, and Jinkyu Jeong. *Proceedings of the 19th USENIX Conference on File and Storage Technologies (FAST 21)*.

~~~~
@inproceedings {264838,
author = {Jiwon Woo and Minwoo Ahn and Gyusun Lee and Jinkyu Jeong},
title = {D2FQ: Device-Direct Fair Queueing for NVMe SSDs},
booktitle = {19th {USENIX} Conference on File and Storage Technologies ({FAST} 21)},
year = {2021},
isbn = {978-1-939133-20-5},
pages = {403--415},
url = {https://www.usenix.org/conference/fast21/presentation/woo},
publisher = {{USENIX} Association},
month = feb,
}
~~~~

## How to Use
1. Compile the kernel with the config `CONFIG_IOSCHED_D2FQ` enabled. If your NVMe device has a plenty of NVMe SQs, you can also enable `CONFIG_IOSCHED_D2FQ_MULTISQ` to allow each core to have isolated three SQs (low, med, high). Without this config, three adjacent cores share three SQs (low, med, high). For more information, please refer to the paper. 

1. To use D2FQ, a file should be opened with a flag `O_D2FQ` which is defined in `uapi/asm-generic/fcntl.h`
