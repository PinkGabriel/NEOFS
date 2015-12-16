/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall neo_fs.c `pkg-config fuse--cflags--libs` -o neo_fs

  Xue Jieyu coded on 2015.9.23 <sanji@mail.ustc.edu.cn>
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
__u32 file_open_list[MAX_OPEN_COUNT];

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

/*function implemented by Xue Jieyu*/

void *neo_init (struct fuse_conn_info *conn)
{
	__u32 groupcnt;
	if ((fp = fopen(diskimg,"rb+")) == NULL) {
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
	bbcache.lastzero = FIRST_FREE_BLOCK;
	memset(bbcache.bbitmap,0,BLOCK_SIZE);
	ibcache.groupnr = -1;
	ibcache.lastzero = 0;
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
	__u16 filetype;
	int res;
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name++;					/*parent_path is parent dir's name,*name is new file's name*/
	if (strlen(name) > 255) {		/*file name too long*/
		res = -ENAMETOOLONG;
		goto mn_err_out;
	}
	if (*parent_path == '\0') {
		parent_ino = 1;
	} else {
		parent_ino = path_resolve(parent_path);
	}
	if (parent_ino == NR_ERROR) {
		res = -errno;
		goto mn_err_out;
	}
	if (search_dentry(parent_ino,name) != NR_ERROR) {/*file already exists*/
		res = -EEXIST;
		goto mn_err_out;
	}
	if ((mode & S_IFREG) == S_IFREG) {
		filetype = 1;
	} else {
		filetype = 2;
	}
	if ((neo_sb_info.s_free_inodes_count > 1) && (neo_sb_info.s_free_blocks_count > 1)) {
		ino = get_inode(parent_ino,filetype);	/*get a inode for reg file*/
	} else {
		res = -ENOSPC;				/*not enough space*/
		goto mn_err_out;
	}
	add_dentry(parent_ino,ino,name,filetype);
	free(parent_path);
	return 0;
mn_err_out:
	free(parent_path);
	return res;
}

static int neo_mkdir(const char *path, mode_t mode)
{
	inode_nr parent_ino;
	inode_nr ino;
	__u16 filetype;
	int res;
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name++;						/*parent_path is parent dir's name,*name is the new dir's name*/
	if (strlen(name) > 255) {			/*dir name too long*/
		res = -ENAMETOOLONG;
		goto md_err_out;
	}
	if (*parent_path == '\0') {
		parent_ino = 1;
	} else {
		parent_ino = path_resolve(parent_path);
	}
	if (parent_ino == NR_ERROR) {
		res = -errno;
		goto md_err_out;
	}
	if (search_dentry(parent_ino,name) != NR_ERROR) {/*file already exists*/
		res = -EEXIST;
		goto md_err_out;
	}
	if ((mode & S_IFREG) == S_IFREG) {
		filetype = 1;
	} else {
		filetype = 2;
	}
	if ((neo_sb_info.s_free_inodes_count > 1) && (neo_sb_info.s_free_blocks_count > 1)) {
		ino = get_inode(parent_ino,filetype);	/*get inode for dir file*/
	} else {
		res = -ENOSPC;				/*not enough space*/
		goto md_err_out;
	}
	add_dentry(parent_ino,ino,name,filetype);
	free(parent_path);
	return 0;
md_err_out:
	free(parent_path);
	return res;
}

/*
static int hello_getattr(const char *path, struct stat *stbuf)
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
	if (ino == NR_ERROR) {
		free(vpath);
		return -errno;
	}
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&inode,sizeof(struct neo_inode),1,fp);
	memset(stbuf, 0, sizeof(struct stat));
	if(inode.i_mode == 1) {
		stbuf->st_mode = S_IFREG | 0666;
		stbuf->st_nlink = 1;
	} else {
		stbuf->st_mode = S_IFDIR | 0666;
		stbuf->st_nlink = 2;
	}
	stbuf->st_ino = ino;
	stbuf->st_uid = inode.i_uid;
	stbuf->st_gid = inode.i_gid; 
	stbuf->st_size = inode.i_size;
	stbuf->st_blocks = inode.i_blocks;
	stbuf->st_ctime = inode.i_ctime;
	stbuf->st_atime = inode.i_atime;
	stbuf->st_mtime = inode.i_mtime;
	
	free(vpath);
	return 0;

}

static int neo_open(const char *path, struct fuse_file_info *fi)
{
	int i;
	char *vpath = strdup(path);
	inode_nr ino = path_resolve(vpath);
	if (ino == NR_ERROR) {
		free(vpath);
		return -errno;
	}
	for (i = 0; i < MAX_OPEN_COUNT; i++) {
		if (file_open_list[i] == 0) {
			break;
		}
	}
	file_open_list[i] = ino;
	fi->fh = i;
	free(vpath);
	return 0;
}

static int neo_opendir (const char *path, struct fuse_file_info *fi)
{
	int i;
	char *vpath = strdup(path);
	inode_nr ino = path_resolve(vpath);
	if (ino == NR_ERROR) {
		free(vpath);
		return -errno;
	}
	for (i = 0; i < MAX_OPEN_COUNT; i++) {
		if (file_open_list[i] == 0) {
			break;
		}
	}
	file_open_list[i] = ino;
	fi->fh = i;
	free(vpath);
	return 0;
}

static int neo_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	int i,n,offset_prev,offset_cur,blkcnt;
	char fname[MAX_FILE_NAME];
	__u64 blkaddr;
	__u32 *p = NULL;
	void *block,*origin = NULL;
	struct stat st;
	struct neo_inode dirinode;
	struct neo_dir_entry *cur;
	inode_nr ino = file_open_list[fi->fh];
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&dirinode,sizeof(struct neo_inode),1,fp);
	if (dirinode.i_blocks == 0) {
		return 0;
	}
	blkcnt = dirinode.i_blocks;
	origin = (void *)malloc(BLOCK_SIZE);
	memset(fname,0,MAX_FILE_NAME);
	if (blkcnt <= 12) {
		n = blkcnt;
	} else {
		n = 12;
	}
	for (i = 0; i < n; i++) {
		blkaddr = block_to_addr(dirinode.i_block[i]);
		fseek(fp,blkaddr,SEEK_SET);
		fread(origin,BLOCK_SIZE,1,fp);
		block = origin;
		cur = origin;
		offset_prev = 0;
		offset_cur = 0;
		if (cur->inode == 0) {
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
			if(cur->file_type == 1) {
				st.st_mode = S_IFREG | 0644;
			} else {
				st.st_mode = S_IFDIR | 0644;
			}

			//if (filler(buf, cur->name, &st, 0))
			//	break;
			filler(buf, fname, &st, 0);
			offset_prev = offset_cur;
			offset_cur += cur->rec_len;
			block += cur->rec_len;
		} while ((offset_prev + cur->rec_len) != 4096);
	}
	if (blkcnt > 12) {
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);
		fseek(fp,block_to_addr(dirinode.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++) {
			blkaddr = block_to_addr(p[i]);
			fseek(fp,blkaddr,SEEK_SET);
			fread(origin,BLOCK_SIZE,1,fp);
			block = origin;
			cur = origin;
			offset_prev = 0;
			offset_cur = 0;
			if (cur->inode == 0) {
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
				if(cur->file_type == 1) {
					st.st_mode = S_IFREG | 0644;
				} else {
					st.st_mode = S_IFDIR | 0644;
				}
				//if (filler(buf, cur->name, &st, 0))
				//	break;
				filler(buf, fname, &st, 0);
				offset_prev = offset_cur;
				offset_cur += cur->rec_len;
				block += cur->rec_len;
			} while ((offset_prev + cur->rec_len) != 4096);
		}
	}
	free(p);
	free(origin);
	return 0;
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
	int res;
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name++;						/*parent_path is parent dir's name,*name is file name*/
	if (strlen(name) > 255) {			/*file name too long*/
		res = -ENAMETOOLONG;
		goto ul_err_out;
	}
	if (*parent_path == '\0') {
		parent_ino = 1;
	} else {
		parent_ino = path_resolve(parent_path);
	}
	if (parent_ino == NR_ERROR) {
		res = -errno;
		goto ul_err_out;
	}
	ino = search_dentry(parent_ino,name);
	if (ino == NR_ERROR) {				/*file not exists*/
		res = -ENOENT;
		goto ul_err_out;
	}
	if (free_inode(ino) == -1) {			/*free inode*/
		res = -errno;
		goto ul_err_out;
	}
	delete_dentry(parent_ino,name,1);
	free(parent_path);
	return 0;
ul_err_out:
	free(parent_path);
	return res;
}

static int neo_rmdir(const char *path)
{
	inode_nr parent_ino;
	inode_nr ino;
	int res;
	char *parent_path = strdup(path);
	char *name = strrchr(parent_path,'/');
	*name = '\0';
	name++;						/*parent_path is parent dir's name,*name is file name*/
	if (strlen(name) > 255) {			/*file name too long*/
		res = -ENAMETOOLONG;
		goto rd_err_out;
	}
	if (*parent_path == '\0') {
		parent_ino = 1;
	} else {
		parent_ino = path_resolve(parent_path);
	}
	if (parent_ino == NR_ERROR) {
		res = -errno;
		goto rd_err_out;
	}
	ino = search_dentry(parent_ino,name);
	if (ino == NR_ERROR) {				/*file not exists*/
		res = -ENOENT;
		goto rd_err_out;
	}
	if (free_inode(ino) == -1) {			/*free inode*/
		res = -errno;
		goto rd_err_out;
	}
	delete_dentry(parent_ino,name,2);
	free(parent_path);
	return 0;
rd_err_out:
	free(parent_path);
	return res;
}

static int neo_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int i,j;


	unsigned int start_blk;				/*start block and offset in block*/
	unsigned int start_r;
	unsigned int end_blk;				/*end block and offset in the last block*/
	unsigned int end_r;

	struct neo_inode read_inode;
	inode_nr ino = file_open_list[fi->fh];

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&read_inode,sizeof(struct neo_inode),1,fp);

	if (SIZE_TO_BLKCNT(offset + size) > read_inode.i_blocks 
				|| offset > read_inode.i_size) {
		return -ESPIPE;
	}
	if (offset + size > read_inode.i_size) {
		size = read_inode.i_size - offset;
	}
	start_blk = offset / BLOCK_SIZE;
	start_r = offset % BLOCK_SIZE;
	end_blk = SIZE_TO_BLKCNT(offset + size) - 1;
	//end_blk = (offset + size - 1) / BLOCK_SIZE;
	end_r = (offset + size) % BLOCK_SIZE;
	if (end_r == 0) {
		end_r = BLOCK_SIZE;
	}

	if (start_blk == end_blk) {
		fseek(fp,(i_block_to_addr(start_blk,read_inode.i_block) + start_r),SEEK_SET);
		fread(buf,size,1,fp);
	} else {
		fseek(fp,(i_block_to_addr(start_blk,read_inode.i_block) + start_r),SEEK_SET);
		fread(buf,(BLOCK_SIZE - start_r),1,fp);
		for (i = (start_blk + 1), j = 0; i <= (end_blk -1); i++,j++) {
			fseek(fp,i_block_to_addr(i,read_inode.i_block),SEEK_SET);
			fread(buf + (BLOCK_SIZE - start_r) + (j * BLOCK_SIZE),BLOCK_SIZE,1,fp);
		}
		fseek(fp,i_block_to_addr(end_blk,read_inode.i_block),SEEK_SET);
		fread(buf + (size - end_r),end_r,1,fp);
	}

	read_inode.i_atime = time(NULL);
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fwrite(&read_inode,sizeof(struct neo_inode),1,fp);
	return size;
}

static int neo_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	block_nr start;
	block_nr end;

	int i,j;

	struct neo_inode write_inode;

	unsigned int start_blk;			/*same with read*/
	unsigned int start_r;
	unsigned int end_blk;
	unsigned int end_r;

	inode_nr ino = file_open_list[fi->fh];
	if ((offset + size) > MAX_FILE_SIZE) {
		return -EFBIG;
	}

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&write_inode,sizeof(struct neo_inode),1,fp);

	start = write_inode.i_blocks;
	end = SIZE_TO_BLKCNT(offset + size);
	if ((offset + size) > write_inode.i_size) {
		write_inode.i_size = offset + size;
		if (end > start) {
			get_selected_blocks(write_inode.i_block,ino,start,(end - 1));
			write_inode.i_blocks = end;
		}
	}
	start_blk = offset / BLOCK_SIZE;
	start_r = offset % BLOCK_SIZE;
	end_blk = SIZE_TO_BLKCNT(offset + size) - 1;
	end_r = (offset + size) % BLOCK_SIZE;
	if (end_r == 0) {
		end_r = BLOCK_SIZE;
	}

	if (start_blk == end_blk) {
		fseek(fp,(i_block_to_addr(start_blk,write_inode.i_block) + start_r),SEEK_SET);
		fwrite(buf,size,1,fp);
	} else {
		fseek(fp,(i_block_to_addr(start_blk,write_inode.i_block) + start_r),SEEK_SET);
		fwrite(buf,(BLOCK_SIZE - start_r),1,fp);
		for (i = (start_blk + 1), j = 0; i <= (end_blk -1); i++,j++){
			fseek(fp,i_block_to_addr(i,write_inode.i_block),SEEK_SET);
			fwrite(buf + (BLOCK_SIZE - start_r) + (j * BLOCK_SIZE),BLOCK_SIZE,1,fp);
		}
		fseek(fp,i_block_to_addr(end_blk,write_inode.i_block),SEEK_SET);
		fwrite(buf + (size - end_r),end_r,1,fp);
	}
	write_inode.i_mtime = time(NULL);
	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fwrite(&write_inode,sizeof(struct neo_inode),1,fp);
	return size;
}

static int neo_truncate(const char *path, off_t size)
{
	block_nr blkcnt;
	struct neo_inode inode;
	char *vpath = strdup(path);
	inode_nr ino = path_resolve(vpath);
	if (ino == NR_ERROR) {
		free(vpath);
		return -errno;
	}
	blkcnt = SIZE_TO_BLKCNT(size);

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fread(&inode,sizeof(struct neo_inode),1,fp);

	inode.i_size = size;
	if (blkcnt > inode.i_blocks) {
		get_selected_blocks(inode.i_block,ino,inode.i_blocks,blkcnt - 1);
	}
	if (blkcnt < inode.i_blocks) {
		free_selected_blocks(inode.i_block,blkcnt,inode.i_blocks - 1);
	}
	inode.i_blocks = blkcnt;

	fseek(fp,inode_to_addr(ino),SEEK_SET);
	fwrite(&inode,sizeof(struct neo_inode),1,fp);

	free(vpath);
	return 0;
}

static int neo_rename(const char *from, const char *to)
{
	int res;
	if (strcmp(from,to) == 0) {
		return 0;
	}
	struct neo_inode pfinode;
	struct neo_inode finode;
	struct neo_inode ptinode;
	struct neo_inode tinode;
	inode_nr parent_fino;
	inode_nr fino;
	inode_nr parent_tino;
	inode_nr tino;
	char *parent_fpath = strdup(from);
	char *parent_tpath = strdup(to);
	char *fname = strrchr(parent_fpath,'/');
	char *tname = strrchr(parent_tpath,'/');
	*fname = '\0';
	fname++;
	*tname = '\0';
	tname++;
	if (strlen(fname) > 255) {	/*from file name too long*/
		res = -ENAMETOOLONG;
		goto rn_err_out;
	}
	if (strlen(tname) > 255) {	/*to file name too long*/
		res = -ENAMETOOLONG;
		goto rn_err_out;
	}
	
	/*deal from file*/
	if (*parent_fpath == '\0') {
		parent_fino = 1;
	} else {
		parent_fino = path_resolve(parent_fpath);
	}
	if (parent_fino == NR_ERROR) {
		res = -errno;
		goto rn_err_out;
	}
	fino = search_dentry(parent_fino,fname);
	if (fino == NR_ERROR) {		/*from file not exist*/
		res = -ENOENT;
		goto rn_err_out;
	}

	/*deal to file*/
	if (*parent_tpath == '\0') {
		parent_tino = 1;
	} else {
		parent_tino = path_resolve(parent_tpath);
	}
	if (parent_tino == NR_ERROR) {	/*to file's parent dir not exist*/
		res = -errno;
		goto rn_err_out;
	}
	tino = search_dentry(parent_tino,tname);

	/*read inodes needed*/
	fseek(fp,inode_to_addr(parent_fino),SEEK_SET);
	fread(&pfinode,sizeof(struct neo_inode),1,fp);	/*get from file parent's inode*/

	fseek(fp,inode_to_addr(fino),SEEK_SET);
	fread(&finode,sizeof(struct neo_inode),1,fp);	/*get from file inode*/
	if (finode.i_mode == 2) {			/*from file can't be to file's ancestor dir*/
		if (strstr(to,from) == to) {
			res = -EINVAL;
			goto rn_err_out;
		}
	}

	fseek(fp,inode_to_addr(parent_tino),SEEK_SET);
	fread(&ptinode,sizeof(struct neo_inode),1,fp);	/*get to file parent's inode*/

	if (tino != NR_ERROR) {
		fseek(fp,inode_to_addr(tino),SEEK_SET);	/*get to file's inode*/
		fread(&tinode,sizeof(struct neo_inode),1,fp);
		if (finode.i_mode == 1 && tinode.i_mode == 2) {		/*from file is a reg file but to file is a dir*/
			res = -EISDIR;
			goto rn_err_out;
		}
		if (finode.i_mode == 2 && tinode.i_mode == 1) {		/*from file is a dir file but to file is a reg*/
			res = -ENOTDIR;
			goto rn_err_out;
		}
		if (tinode.i_mode == 2 && tinode.i_blocks != 0) {	/*to file is a dir and is not empty*/
			res = -ENOTEMPTY;
			goto rn_err_out;
		}

		/*to file already exists*/
		free_inode(tino);
		delete_dentry(parent_tino,tname,tinode.i_mode);
		delete_dentry(parent_fino,fname,finode.i_mode);
		add_dentry(parent_tino,fino,tname,finode.i_mode);
	} else {
		/*to file not exists*/
		delete_dentry(parent_fino,fname,finode.i_mode);
		add_dentry(parent_tino,fino,tname,finode.i_mode);
	}
	pfinode.i_ctime = time(NULL);
	ptinode.i_ctime = time(NULL);
	fseek(fp,inode_to_addr(parent_fino),SEEK_SET);
	fwrite(&pfinode,sizeof(struct neo_inode),1,fp);
	fseek(fp,inode_to_addr(parent_tino),SEEK_SET);
	fwrite(&ptinode,sizeof(struct neo_inode),1,fp);
	free(parent_fpath);
	free(parent_tpath);
	return 0;
rn_err_out:
	free(parent_fpath);
	free(parent_tpath);
	return res;
}

static int neo_flush(const char *path, struct fuse_file_info *fi)
{
	write_bitmap();
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

int neo_utimens(const char *path, const struct timespec ts[2])
{
	return 0;
}

void neo_destroy(void *p)
{
	/*write sb and gdt to main copy and backups*/
	/*write bbtimap and ibitmap to the disk*/
	write_bitmap();
	free(neo_gdt);
	fclose(fp);
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
	.utimens	= neo_utimens,
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

