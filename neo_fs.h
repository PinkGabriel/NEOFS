/*
 * neo_fs.h
 * Xue Jieyu developped in 2015.12
 * email:sanji@mail.ustc.edu.cn
 */

#ifndef NEO_FS_H
#define NEO_FS_H

#define NEO_BLOCKS 15
#define MAX_FILE_NAME 255
#define MAX_FILE_SIZE 4294967296
#define MAX_FS_SIZE 17179869184
#define INODES_PER_GROUP 8192
#define BLOCKS_PER_GROUP (BLOCK_SIZE * 8)
#define BLOCK_SIZE (1024<<LOG_BLOCK_SIZE)
#define LOG_BLOCK_SIZE 2
#define DISKIMG "diskimg"
#define MAX_OPEN_COUNT 256
#define FIRST_FREE_BLOCK 32	/* metadata used 256+4=260 blocks 260/8 = 32 */
#define ROOT_ADDR 16512		/* root is inode 1,ie,4blocks+128(inode 0 reserved) = 4*4096+128 = 16512 */
#define NR_ERROR 0xFFFFFFFF	/* inode or block number error,max of unsigned int */

/* direct index，1-indirect index，2-indirect index block number:0~11,12~1035,1036~1049611 */
#define DIRECT_INDEX_END 11
#define IN_INDEX_BGN 12
#define IN_INDEX_END 1035
#define ININ_INDEX_BGN 1036
#define ININ_INDEX_END 1049611

typedef unsigned long	__u64;
typedef unsigned int	__u32;
typedef unsigned short	__u16;
typedef unsigned char	__u8;
#define block_nr __u32
#define inode_nr __u32
#define bg_nr __u32

/* caculate the true length of dir entry in the storage by the name length x*/
#define TRUE_LEN(x) ((((x)%4)?(4 - (x)%4 + (x)):(x)) + 8)
/* caculate the total blocks needed by the file size x */
#define SIZE_TO_BLKCNT(x) (((x)%4096)?((x)/4096 + 1):((x)/4096))

/*
 * Super block for the filesystem,included all the essential informations
 * total size:38Bytes
 * __u32:unsigned int; __u16:unsigned short; __u8:unsigned char
 */
struct neo_super_block
{
	__u32  s_inodes_count;		/* Inodes count */
	__u32  s_blocks_count;		/* Blocks count */
	__u32  s_groups_count;		/* Groups count */
	__u32  s_free_inodes_count;	/* Free inodes count */
	__u32  s_free_blocks_count;	/* Free blocks count */
	__u32  s_log_block_size;	/* Block size */
	__u32  s_blocks_per_group;	/* # Blocks per group */
	__u32  s_inodes_per_group;	/* # Inodes per group */
	__u16  s_magic;			/* Magic signature */
	__u16  s_inode_size;		/* size of inode structure */
	__u16  s_block_group_nr;	/* block group # of this superblock */
};

/*
 * Group descriptor for each block group,includes the current status of the group
 * total size:32Bytes
 * one block is 4KB，can store 128 group descriptors，so in this system,we use
 * one block to store GDT,the filesystem's max size is 128*128MB=16GB 
 */
struct neo_group_desc
{
	__u32  bg_block_bitmap;		/* Blocks bitmap block */
	__u32  bg_inode_bitmap;		/* Inodes bitmap block */
	__u32  bg_inode_table;		/* Inodes table block */
	__u16  bg_free_blocks_count;	/* Free blocks count */
	__u16  bg_free_inodes_count;	/* Free inodes count */
	__u16  bg_used_dirs_count;	/* Directories count */
	__u16  bg_pad;			/* 16bits for padding,align to 32Byte */
	__u32  bg_reserved[3];		/* 3 * 32 = 12Byte reserved and meanwhile align to 32Byte */
};

/*
 * inode describe attributes for a file
 * total size:128Bytes
 * one block stores 4K/128Byte = 32 inodes,if we use a whole block for inode bitmap
 * in one group,the inode table would occupy 4K * 8 / 32 = 1K blocks,ie,4MB.That is
 * a little too large for 128MB.so we make the number of inodes in one group 4K rather than 32K,
 * so the metadate of inode table occupy 1MB,blanced for 128MB relatively
 */
struct neo_inode
{
	__u32 i_uid;			/* 32bits，uid */
	__u32 i_gid;			/* 32bits，gid */
	__u32 i_size;			/* 32bits，this limits file size max to 4GB */
	__u32 i_blocks;			/* 32bits，used blocks number */
	__time_t i_atime;		/* last time inode was accessed two long type，16Byte */
	__time_t i_ctime;		/* when the inode was created. */
	__time_t i_mtime;		/* last time inode was modified */
	__u32 i_block[NEO_BLOCKS];	/* index to store data */
	__u16 i_mode;			/* 16bits,1 means regular file,2 means dir */
	__u16  bg_pad;			/* 16bits for padding,align to 128 */
	__u64  bg_reserved[3];		/* for reserved and meanwhile align to 32Byte */
};

/*
 * dir entry for a file,to build the dir structure
 * total size:variable length (8 + namelength) Bytes
 */
struct neo_dir_entry
{
	__u32   inode;			/* Inode number */
	__u16   rec_len;		/* Directory entry length */
	__u8    name_len;		/* Name length */
	__u8    file_type;		/* extented in revision 1 */
	char    name[MAX_FILE_NAME];   	/* File name aligns to 4Bytes */
};

/*
 * bitmap cache in the memory
 */
struct block_bitmap_cache
{
	int groupnr;
	unsigned char bbitmap[BLOCK_SIZE];
};

struct inode_bitmap_cache
{
	int groupnr;
	unsigned char ibitmap[BLOCK_SIZE];
};
#endif









