#ifndef OP_H
#define OP_H

#include "neo_fs.h"

extern FILE *fp;

extern struct neo_super_block neo_sb_info;

extern struct neo_group_desc *neo_gdt;

extern inode_nr path_resolve(char *path);

extern inode_nr search_dentry(inode_nr ino, char *name);

extern block_nr get_block(char *path);

extern void print_sb(struct neo_super_block neo_sb_info);

extern void print_gdt(struct neo_group_desc *gdt,int groupcnt);

extern void print_inode(struct neo_inode ino);

extern __u64 inode_to_addr(inode_nr ino);

extern int is_powerof_357(int i);

#endif
