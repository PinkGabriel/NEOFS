#ifndef OP_H
#define OP_H

#include "neo_fs.h"

extern FILE *fp;

extern __u16 file_open_list[MAX_OPEN_COUNT];

extern struct neo_super_block neo_sb_info;

extern struct neo_group_desc *neo_gdt;

extern struct block_bitmap_cache bbcache;

extern struct inode_bitmap_cache ibcache;

extern inode_nr path_resolve(char *path);

extern inode_nr search_dentry(inode_nr ino, char *name);

extern int add_dentry(inode_nr parent_ino,inode_nr ino,char * name,__u16 i_mode);

extern int delete_dentry(inode_nr parent_ino,char *name,__u16 i_mode);

extern void delete_block_dentry(inode_nr parent_ino,int blknr,__u64 blkaddr,unsigned int info[]);


extern block_nr get_block(inode_nr ino);

extern void free_block(block_nr blk);

extern void get_selected_blocks(__u32 *i_block,inode_nr ino,__u32 start,__u32 end);

extern void free_selected_blocks(__u32 *i_block,__u32 start,__u32 end);

extern inode_nr get_inode(inode_nr ino,__u16 i_mode);

extern void init_inode(inode_nr res,__u16 i_mode);

extern int free_inode(inode_nr ino);

extern void print_sb(struct neo_super_block neo_sb_info);

extern void print_gdt(struct neo_group_desc *gdt,int groupcnt);

extern void print_inode(struct neo_inode ino);

extern __u64 inode_to_addr(inode_nr ino);

extern __u64 i_block_to_addr(block_nr blknr,block_nr i_block[]);

extern __u64 inline block_to_addr(block_nr blk);

extern int blk_search_dentry(__u64 blkaddr,char *name,unsigned int info[]);

extern int blk_search_empty_dentry(__u64 blkaddr,char *name,unsigned int info[]);

extern void write_dentry(__u64 blkaddr,unsigned int info[],struct neo_dir_entry dirent);

extern void write_sb_gdt_main(bg_nr bgnr);

extern void write_sb_gdt_backups();

extern void write_bitmap();

extern int is_powerof_357(int i);

#endif
