#include<math.h>

#define NEO_BLOCKS 15
#define MAX_FS_SIZE 17179869184
#define BLOCKS_PER_GROUP (BLOCK_SIZE * 8)
#define BLOCK_SIZE (1024<<LOG_BLOCK_SIZE)
#define LOG_BLOCK_SIZE 2
#define FIRST_FREE_BLOCK 260	/*metadata used 256+4=260 blocks*/

//#define bgid_to_bgaddr(bg) ()

typedef unsigned long	__u64;
typedef unsigned int	__u32;
typedef unsigned short	__u16;
typedef unsigned char	__u8;

struct neo_super_block
{/*36Byte __u32:unsigned int; __u16:unsigned short; __u8:unsigned char */
	__u32  s_inodes_count;		/* Inodes count */
	__u32  s_blocks_count;		/* Blocks count */
	__u32  s_free_inodes_count;	/* Free inodes count */
	__u32  s_free_blocks_count;	/* Free blocks count */
	__u32  s_log_block_size;	/* Block size */
	__u32  s_blocks_per_group;	/* # Blocks per group */
	__u32  s_inodes_per_group;	/* # Inodes per group */
	__u16  s_magic;			/* Magic signature */
	__u16  s_inode_size;		/* size of inode structure */
	__u16  s_block_group_nr;	/* block group # of this superblock */
};

struct neo_group_desc
{/*32Byte，一个block4KB，128个GD，故如果GDT存储于一个block则文件系统大小上限128*128MB=16GB*/
	__u32  bg_block_bitmap;		/* Blocks bitmap block */
	__u32  bg_inode_bitmap;		/* Inodes bitmap block */
	__u32  bg_inode_table;		/* Inodes table block */
	__u16  bg_free_blocks_count;	/* Free blocks count */
	__u16  bg_free_inodes_count;	/* Free inodes count */
	__u16  bg_used_dirs_count;	/* Directories count */
	__u16  bg_pad;			/*16bit用于填充结构的32bit边界。*/
	__u32  bg_reserved[3];		/*3个保留域配上上一个pad正好14Byte*/
};

struct neo_inode
{/*2 + 3 + 7 + 1 = 13，+pad=128Byte,故一个block可存4K/128Byte=32个,4Kblock一个块组32K个inode，存储占用32K/32=1K个block即4MB
  *这样128M用了4M放inode，有点多，故inode位图只使用1/4，即8K个inode，此时一个块组inode占用1MB，256个block。*/
	__u32 i_uid;			/*32bits，uid*/
	__u32 i_gid;			/*32bits，gid*/
	__u32 i_size;			/*32bits，字节单位的文件长度限制了单个文件最大4G*/
	__u32 i_blocks;			/*32bits，用的4Kblock的个数*/
	__time_t i_atime;		/*last time inode was accessed 两个long，16Byte */
	__time_t i_ctime;		/* when the inode was created.*/
	__time_t i_mtime;		/* last time inode was modified*/
	__u32 i_block[NEO_BLOCKS];
	__u16 i_mode;			/*16bits,1 means regular file,2 means dir*/
	__u16  bg_pad;			/*16bit配合上边的32+16对齐8Byte*/
	__u64  bg_reserved[3];		/*3个保留域配上上一个pad正好16Byte*/
};

int is_powerof_357(int i)
{
	if (pow(3,(int)(float)(log(i)/log(3))) == i || pow(5,(int)(float)(log(i)/log(5))) == i || 
		pow(7,(int)(float)(log(i)/log(7))) == i || i == 0)
		return 1;
	return 0;
}

