/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall neo_fs.c `pkg-config fuse --cflags --libs` -o neo_fs

  xuejieyu coded on 2015.9.23 <sanji@mail.ustc.edu.cn>
  alright=.=..begin coding on 2015.11.3
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

/*header*/
#include <syslog.h>
#include "atomic_ops.h"
#include "neo_fs.h"

/*global variable*/
FILE *fp = NULL;
char *diskimg = NULL;
struct neo_super_block neo_sb_info;
struct neo_group_desc *neo_gdt;
struct block_bitmap_cache bbcache;
struct inode_bitmap_cache ibcache;
__u16 file_open_list[MAX_OPEN_COUNT];

/*
static int neo_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

static int neo_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int neo_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

*/

#ifdef HAVE_UTIMENSAT
static int neo_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

#ifdef HAVE_POSIX_FALLOCATE
static int neo_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int neo_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int neo_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int neo_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int neo_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

/*function implemented by user*/

void *neo_init (struct fuse_conn_info *conn)
{
	int groupcnt;
	//if ((fp = fopen(DISKIMG,"rb+")) == NULL){
	if ((fp = fopen(diskimg,"rb+")) == NULL){
		printf("image file open failed\n");
		exit(1);
	}
	fseek(fp,1024,SEEK_SET);
	fread(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	groupcnt = neo_sb_info.s_groups_count;
	fseek(fp,4096,SEEK_SET);
	neo_gdt = (struct neo_group_desc *)malloc(sizeof(struct neo_group_desc) * groupcnt);
	fread(neo_gdt,sizeof(struct neo_group_desc),groupcnt,fp);

	bbcache.groupnr = -1;
	memset(bbcache.bbitmap,0,BLOCK_SIZE);
	ibcache.groupnr = -1;
	memset(ibcache.ibitmap,0,BLOCK_SIZE);
	memset(file_open_list,0,MAX_OPEN_COUNT);

	print_sb(neo_sb_info);
	print_gdt(neo_gdt,groupcnt);

	return 0;
}

static int neo_mknod(const char *path, mode_t mode, dev_t rdev)
{
	inode_nr parent_ino;
	inode_nr ino;
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name ++;					/*此时parent_path是父目录路径，name为目标文件名*/
	if (strlen(name) > 255)				/*文件名太长*/
		return -ENAMETOOLONG;
	if (*parent_path == '\0')
		parent_ino = 1;
	else
		parent_ino = path_resolve(parent_path);
	if (search_dentry(parent_ino,name))		/*文件已存在*/
		return -EEXIST;
	if ((neo_sb_info.s_free_inodes_count > 1) && (neo_sb_info.s_free_blocks_count > 1))
		ino = get_inode(parent_ino,1);		/*按照普通文件分配一个inode*/
	else
		return -ENOSPC;				/*空间不足*/
	add_dentry(parent_ino,ino,name,1);
	return 0;

	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
/*	if (S_ISREG(mode)) {
		res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
*/
}

static int neo_mkdir(const char *path, mode_t mode)
{
	inode_nr parent_ino;
	inode_nr ino;
	//syslog(LOG_INFO,"mkdir path %s",path);
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name ++;					/*此时parent_path是父目录路径，name为目标文件名*/
	if (strlen(name) > 255)				/*文件名太长*/
		return -ENAMETOOLONG;
	if (*parent_path == '\0')
		parent_ino = 1;
	else
		parent_ino = path_resolve(parent_path);
	if (search_dentry(parent_ino,name))		/*文件已存在*/
		return -EEXIST;
	if ((neo_sb_info.s_free_inodes_count > 1) && (neo_sb_info.s_free_blocks_count > 1))
		ino = get_inode(parent_ino,2);		/*按照普通文件分配一个inode*/
	else
		return -ENOSPC;				/*空间不足*/
	add_dentry(parent_ino,ino,name,2);
	return 0;
}

/*
static int neo_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;
	char *hello_str = "Hello World!\n";
	char *hello_path = "/hello";

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, hello_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
	} else
		res = -ENOENT;

	return res;
}
*/

static int neo_getattr(const char *path, struct stat *stbuf)
{
	struct neo_inode inode;
	char *vpath;
	inode_nr ino;
	vpath = strdup(path);
	ino = path_resolve(vpath);
	//syslog(LOG_INFO,"getattr path %s",vpath);
	if (ino == 0)
		return -errno;
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&inode,sizeof(struct neo_inode),1,fp);
	memset(stbuf, 0, sizeof(struct stat));
	if(inode.i_mode == 1)
		stbuf->st_mode = S_IFREG | 0644;
	else
		stbuf->st_mode = S_IFDIR | 0644;
	stbuf->st_ino = ino;
	//syslog(LOG_INFO,"getattr inode %lu",stbuf->st_ino);
	if (ino == 1)
		stbuf->st_nlink = 2;
	else
		stbuf->st_nlink = 1;
	stbuf->st_uid = inode.i_uid;
	stbuf->st_gid = inode.i_gid; 
	stbuf->st_size = inode.i_size;
	stbuf->st_blocks = inode.i_blocks;
	stbuf->st_ctime = inode.i_ctime;
	stbuf->st_atime = inode.i_atime;
	stbuf->st_mtime = inode.i_mtime;
	//syslog(LOG_INFO,"getattr uid %d",inode.i_uid);

	return 0;

}

static int neo_open(const char *path, struct fuse_file_info *fi)
{
	int i;
	char *vpath = strdup(path);
	inode_nr ino = path_resolve(vpath);
	//syslog(LOG_INFO,"open path %s",vpath);
	if (ino == 0)
		return -errno;
	for (i = 0; i < MAX_OPEN_COUNT; i++){
		if (file_open_list[i] == 0)
			break;
	}
	file_open_list[i] = ino;
	fi->fh = i;
	//syslog(LOG_INFO,"open fh %lu",fi->fh);
	return 0;
/*
	int res;

	res = open(path, fi->flags);
	if (res == -1)
		return -errno;

	close(res);
	fi->fh = 333;
	return 0;
*/
}

static int neo_opendir (const char *path, struct fuse_file_info *fi)
{
	int i;
	char *vpath = strdup(path);
	//syslog(LOG_INFO,"opendir path %s",vpath);
	inode_nr ino = path_resolve(vpath);
	if (ino == 0)
		return -errno;
	for (i = 0; i < MAX_OPEN_COUNT; i++){
		if (file_open_list[i] == 0)
			break;
	}
	file_open_list[i] = ino;
	fi->fh = i;
	//syslog(LOG_INFO,"opendir inode %u",ino);
	//syslog(LOG_INFO,"opendir fh %lu",fi->fh);
	return 0;
}

static int neo_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	int i,n,offset_prev,offset_cur,blkcnt;
	char fname[MAX_FILE_NAME];
	__u64 blkaddr;
	__u32 *p = NULL;
	void *block;
	struct stat st;
	struct neo_inode dirinode;
	struct neo_dir_entry *cur;
	//syslog(LOG_INFO,"readdir get fh %lu",fi->fh);
	inode_nr ino = file_open_list[fi->fh];
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&dirinode,sizeof(struct neo_inode),1,fp);
	if (dirinode.i_blocks == 0)
		return 0;
	blkcnt = dirinode.i_blocks;
	block = (void *)malloc(BLOCK_SIZE);
	cur = block;
	offset_prev = 0;
	offset_cur = 0;
	memset(fname,0,MAX_FILE_NAME);
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(dirinode.i_block[i]);
		fseek(fp,blkaddr,SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		if (cur->inode == 0){
			offset_prev += cur->rec_len;
			offset_cur += cur->rec_len;
			block += cur->rec_len;
			cur = block;
		}
		do {
			cur = block;
			memset(&st, 0, sizeof(st));
			memset(fname,0,MAX_FILE_NAME);
			strncpy(fname,cur->name,cur->name_len);
			st.st_ino = cur->inode;
			//syslog(LOG_INFO,"readdir st inode %lu",st.st_ino);
			if(cur->file_type == 1)
				st.st_mode = S_IFREG | 0644;
			else
				st.st_mode = S_IFDIR | 0644;

			//syslog(LOG_INFO,"readdir_name %s",fname);
			//if (filler(buf, cur->name, &st, 0))
			//	break;
			filler(buf, fname, &st, 0);
			offset_prev = offset_cur;
			offset_cur += cur->rec_len;
			block += cur->rec_len;
		}
		while ((offset_prev + cur->rec_len) != 4096);
	}
	if (blkcnt > 12){//dir file's max blocks count is 13,block[12] for indirect addr.
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);	//4 = sizeof(__32)
		fseek(fp,block_to_addr(dirinode.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			fseek(fp,blkaddr,SEEK_SET);
			fread(block,BLOCK_SIZE,1,fp);
			if (cur->inode == 0){
				offset_prev += cur->rec_len;
				offset_cur += cur->rec_len;
				block += cur->rec_len;
				cur = block;
			}
			do {
				cur = block;
				memset(&st, 0, sizeof(st));
				memset(fname,0,MAX_FILE_NAME);
				strncpy(fname,cur->name,cur->name_len);
				st.st_ino = cur->inode;
				//st.st_mode = cur->file_type;
				if(cur->file_type == 1)
					st.st_mode = S_IFREG | 0644;
				else
					st.st_mode = S_IFDIR | 0644;
				//if (filler(buf, cur->name, &st, 0))
				//	break;
				filler(buf, fname, &st, 0);
				offset_prev = offset_cur;
				offset_cur += cur->rec_len;
				block += cur->rec_len;
			}
			while ((offset_prev + cur->rec_len) != 4096);
		}
		free(p);
	}
	return 0;
/*
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;

	dp = opendir(path);
	if (dp == NULL)
		return -errno;

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0))
			break;
	}

	closedir(dp);
	return 0;
*/
}

static int neo_releasedir(const char *path, struct fuse_file_info *fi)
{
	file_open_list[fi->fh] = 0;
	return 0;
}

static int neo_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	file_open_list[fi->fh] = 0;
	return 0;
}

static int neo_unlink(const char *path)
{
	inode_nr parent_ino;
	inode_nr ino;
	//syslog(LOG_INFO,"unlink path %s",path);
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name ++;					/*此时parent_path是父目录路径，name为目标文件名*/
	if (strlen(name) > 255)				/*文件名太长*/
		return -ENAMETOOLONG;
	if (*parent_path == '\0')
		parent_ino = 1;
	else
		parent_ino = path_resolve(parent_path);
	ino = search_dentry(parent_ino,name);
	if (ino == 0)					/*文件不存在*/
		return -ENOENT;
	//syslog(LOG_INFO,"unlink parent_ino %d",ino);
	//syslog(LOG_INFO,"unlink ino %d",ino);
	//syslog(LOG_INFO,"unlink name %s",name);
	if (free_inode(ino) == -1)				/*按照普通文件分配一个inode*/
		return -errno;
	delete_dentry(parent_ino,name,1);
	return 0;
}

static int neo_rmdir(const char *path)
{
	inode_nr parent_ino;
	inode_nr ino;
	//syslog(LOG_INFO,"rmdir path %s",path);
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name ++;					/*此时parent_path是父目录路径，name为目标文件名*/
	if (strlen(name) > 255)				/*文件名太长*/
		return -ENAMETOOLONG;
	if (*parent_path == '\0')
		parent_ino = 1;
	else
		parent_ino = path_resolve(parent_path);
	ino = search_dentry(parent_ino,name);
	if (ino == 0)					/*文件不存在*/
		return -ENOENT;
	if (free_inode(ino) == -1)				/*按照普通文件分配一个inode*/
		return -errno;
	delete_dentry(parent_ino,name,2);
	return 0;
}

static int neo_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int i,j;


	unsigned int start_blk;	/*写入的内容的起始和结束位置的块号和在块中的偏移*/
	unsigned int start_r;
	unsigned int end_blk;
	unsigned int end_r;

	struct neo_inode read_inode;
	inode_nr ino = file_open_list[fi->fh];

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&read_inode,sizeof(struct neo_inode),1,fp);

	if (SIZE_TO_BLKCNT(offset + size) > read_inode.i_blocks){
		return -ESPIPE;
	}
	start_blk = offset / BLOCK_SIZE;
	start_r = offset % BLOCK_SIZE;
	end_blk = SIZE_TO_BLKCNT(offset + size) - 1;
	//end_blk = (offset + size - 1) / BLOCK_SIZE;
	end_r = (offset + size) % BLOCK_SIZE;
	if (end_r == 0)
		end_r = BLOCK_SIZE;

	if (start_blk == end_blk){
		fseek(fp,(i_block_to_addr(start_blk,read_inode.i_block) + start_r),SEEK_SET);
		fread(buf,size,1,fp);
	}else {
		fseek(fp,(i_block_to_addr(start_blk,read_inode.i_block) + start_r),SEEK_SET);
		fread(buf,(BLOCK_SIZE - start_r),1,fp);
		for (i = (start_blk + 1), j = 0; i <= (end_blk -1); i ++,j ++){
			fseek(fp,i_block_to_addr(i,read_inode.i_block),SEEK_SET);
			fread(buf + (BLOCK_SIZE - start_r) + (j * BLOCK_SIZE),BLOCK_SIZE,1,fp);
		}
		fseek(fp,i_block_to_addr(end_blk,read_inode.i_block),SEEK_SET);
		fread(buf + (size - end_r),end_r,1,fp);
	}

	return size;

/*
	int fd;
	int res;

	(void) fi;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -errno;

	res = pread(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
*/
}

static int neo_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{/*直接索引，1级间接，二级间接对应块区间分别是：0~11,12~1035,1036~1049611*/
	block_nr start;
	block_nr end;

	int i,j;

	struct neo_inode write_inode;

	unsigned int start_blk;	/*写入的内容的起始和结束位置的块号和在块中的偏移*/
	unsigned int start_r;
	unsigned int end_blk;
	unsigned int end_r;

	inode_nr ino = file_open_list[fi->fh];
	if ((offset + size) > MAX_FILE_SIZE){
		return -EFBIG;
	}

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&write_inode,sizeof(struct neo_inode),1,fp);

	start = write_inode.i_blocks;
	end = SIZE_TO_BLKCNT(offset + size);
	if ((offset + size) > write_inode.i_size){
		write_inode.i_size = offset + size;
		if (end > start){
			get_selected_blocks(write_inode.i_block,ino,start,(end - 1));
			write_inode.i_blocks = end;
		}
	}
	start_blk = offset / BLOCK_SIZE;
	start_r = offset % BLOCK_SIZE;
	end_blk = SIZE_TO_BLKCNT(offset + size) - 1;
	end_r = (offset + size) % BLOCK_SIZE;
	if (end_r == 0)
		end_r = BLOCK_SIZE;

	if (start_blk == end_blk){
		fseek(fp,(i_block_to_addr(start_blk,write_inode.i_block) + start_r),SEEK_SET);
		fwrite(buf,size,1,fp);
	}else {
		fseek(fp,(i_block_to_addr(start_blk,write_inode.i_block) + start_r),SEEK_SET);
		fwrite(buf,(BLOCK_SIZE - start_r),1,fp);
		for (i = (start_blk + 1), j = 0; i <= (end_blk -1); i ++,j ++){
			fseek(fp,i_block_to_addr(i,write_inode.i_block),SEEK_SET);
			fwrite(buf + (BLOCK_SIZE - start_r) + (j * BLOCK_SIZE),BLOCK_SIZE,1,fp);
		}
		fseek(fp,i_block_to_addr(end_blk,write_inode.i_block),SEEK_SET);
		//fwrite(buf + (BLOCK_SIZE - start_r) + (j * BLOCK_SIZE),end_r,1,fp);
		fwrite(buf + (size - end_r),end_r,1,fp);
	}
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fwrite(&write_inode,sizeof(struct neo_inode),1,fp);
	return size;
/*
	int fd;
	int res;

	(void) fi;
	fd = open(path, O_WRONLY);
	if (fd == -1)
		return -errno;

	res = pwrite(fd, buf, size, offset);
	if (res == -1)
		res = -errno;

	close(fd);
	return res;
*/
}

static int neo_truncate(const char *path, off_t size)
{
	block_nr blkcnt;
	struct neo_inode inode;
	char *vpath = strdup(path);
	inode_nr ino = path_resolve(vpath);
	blkcnt = SIZE_TO_BLKCNT(size);

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&inode,sizeof(struct neo_inode),1,fp);

	inode.i_size = size;
	inode.i_blocks = blkcnt;
	if (blkcnt > inode.i_blocks){
		get_selected_blocks(inode.i_block,ino,inode.i_blocks,blkcnt - 1);
	}
	if (blkcnt < inode.i_blocks){
		free_selected_blocks(inode.i_block,blkcnt,inode.i_blocks - 1);
	}

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fwrite(&inode,sizeof(struct neo_inode),1,fp);

	return 0;

/*
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
*/
}

static int neo_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int neo_flush(const char *path, struct fuse_file_info *fi)
{
	return 0;
}

static int neo_access(const char *path, int mask)
{
	return 0;
}

static int neo_chmod(const char *path, mode_t mode)
{
	return 0;
}

static int neo_chown(const char *path, uid_t uid, gid_t gid)
{
	return 0;
}

static int neo_statfs(const char *path, struct statvfs *stbuf)
{
	stbuf->f_bsize = BLOCK_SIZE;				/* file system block size */
	//stbuf->f_frsize;					/* fragment size */
	stbuf->f_blocks = neo_sb_info.s_blocks_count;   	/* size of fs in f_frsize units */
	stbuf->f_bfree = neo_sb_info.s_free_blocks_count;	/* # free blocks */
	stbuf->f_bavail = neo_sb_info.s_free_blocks_count;	/* # free blocks for non-root */
	stbuf->f_files = neo_sb_info.s_inodes_count;		/* # inodes */
	stbuf->f_ffree = neo_sb_info.s_free_inodes_count;	/* # free inodes */
	stbuf->f_favail = neo_sb_info.s_free_inodes_count;	/* # free inodes for non-root */
	//stbuf->f_fsid;					/* file system ID */
	//stbuf->f_flag;					/* mount flags */
	stbuf->f_namemax = MAX_FILE_NAME;			/* maximum filename length */
	return 0;
}

static int neo_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
/*
	(void) path;
	(void) isdatasync;
	(void) fi;
*/
	return 0;
}

void neo_destroy(void *p)
{
	/*write sb and gdt to main copy and backups*/
	/*write bbtimap and ibitmap to the disk*/
	write_bitmap();
	printf("\ndestroy functions complete.\n");
}

static struct fuse_operations neo_oper = {

	.init		= neo_init,
	.getattr	= neo_getattr,
	.mknod		= neo_mknod,
	.destroy	= neo_destroy,
	.open		= neo_open,
	.opendir	= neo_opendir,
	.readdir	= neo_readdir,
	.releasedir     = neo_releasedir,
	.release	= neo_release,
	.flush		= neo_flush,
	.mkdir		= neo_mkdir,
	.unlink		= neo_unlink,
	.rmdir		= neo_rmdir,
	.read		= neo_read,
	.write		= neo_write,
	.rename		= neo_rename,
	.truncate	= neo_truncate,
	.access		= neo_access,
	.chmod		= neo_chmod,
	.chown		= neo_chown,
	.statfs		= neo_statfs,
	.fsync		= neo_fsync,
/*
	.readlink	= neo_readlink,
	.symlink	= neo_symlink,
	.link		= neo_link,
#ifdef HAVE_UTIMENSAT
	.utimens	= neo_utimens,
#endif
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= neo_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= neo_setxattr,
	.getxattr	= neo_getxattr,
	.listxattr	= neo_listxattr,
	.removexattr	= neo_removexattr,
#endif
*/
};

int main(int argc, char *argv[])
{
	diskimg = strdup(argv[--argc]);
	return fuse_main(argc, argv, &neo_oper, NULL);
}
