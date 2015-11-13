gcc format.c -lm -o format
gcc dumpneofs.c -lm -o dumpneofs
gcc module_test.c atomic_ops.c -o module_test -lm -g 
gcc atomic_ops.c -lm -c
gcc neo_fs.c -Wall `pkg-config fuse --cflags --libs` -c
gcc neo_fs.o atomic_ops.o `pkg-config fuse --cflags --libs` -lm -o neo_fs
#gcc -Wall -lm neo_fs.c atomic_ops.c `pkg-config fuse --cflags --libs` -o neo_fs
