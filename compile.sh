gcc format.c -lm -Wall -o format
gcc dumpneofs.c -lm -Wall -o dumpneofs
gcc module_test.c atomic_ops.c -o module_test -lm -Wall -g 
gcc atomic_ops.c -lm -Wall -c -g
gcc neo_fs.c -Wall `pkg-config fuse --cflags --libs` -c -Wall -g
gcc neo_fs.o atomic_ops.o `pkg-config fuse --cflags --libs` -lm -Wall -g -o neo_fs
#gcc -Wall -lm neo_fs.c atomic_ops.c `pkg-config fuse --cflags --libs` -o neo_fs
