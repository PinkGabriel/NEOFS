gcc format.c -lm -o format
gcc dumpneofs.c -lm -o dumpneofs
gcc module_test.c atomic_ops.c -o module_test -lm -g 
#gcc -Wall -lm neo_fs.c atomic_ops.c `pkg-config fuse --cflags --libs` -o neo_fs
