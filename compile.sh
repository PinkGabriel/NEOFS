gcc format.c -lm -o format
gcc dumpneofs.c -lm -o dumpneofs
gcc module_test.c atomic_ops.c -lm -o module_test
#gcc -Wall -lm neo_fs.c atomic_ops.c `pkg-config fuse --cflags --libs` -o neo_fs
