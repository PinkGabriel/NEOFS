#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atomic_ops.h"
#include "neo_fs.h"
#include <errno.h>
#include <time.h>
#include <math.h>
#include <syslog.h>
#include<unistd.h>

/*
 * path_resolve - resolve file path to inode
 * @path:file full path
 */
inode_nr path_resolve(char *path)
{
	inode_nr parent,res;
	int length;
	char *pos;
	char tmp[MAX_FILE_NAME];
	if (strcmp(path,"/") == 0) {	/* root's inode is 1. 0 is reserved for judge */
		return 1;
	}
	path++;
	parent = 1;
	while ((pos = strchr(path,'/')) != NULL) {
		memset(tmp,0,MAX_FILE_NAME);
		if ((length = (pos - path)) > 255) {
			errno = ENAMETOOLONG;
			return NR_ERROR;
		}
		strncpy(tmp,path,pos - path);
		parent = search_dentry(parent,tmp);
		if (parent == NR_ERROR) {
			errno = ENOENT;
			return NR_ERROR;
		}
		path =++pos;
	}
	if ((res = search_dentry(parent,path)) != NR_ERROR) {
		return res;
	} else {
		errno = ENOENT;
		return NR_ERROR;
	}
}

/*
 * search_dentry - get file's inode by file name and parent dir's inode
 * @ino:parent dir's inode
 * @name:file's name
 */
inode_nr search_dentry(inode_nr ino, char *name)
{
	unsigned int blkcnt;
	/*
	 * info[0] is target's inode 
	 * info[1] is dentry's rec_len
	 * info[2] is prev dentry's offset in the block 
	 * info[3] is current dentry's offset in the block
	 */
	unsigned int info[4] = {0};
	inode_nr res = NR_ERROR;
	__u64 inoaddr;
	__u64 blkaddr;
	block_nr *p = NULL;			/* store indirect block number */
	struct neo_inode dirinode;
	int i,n;
	inoaddr = inode_to_addr(ino);
	fseek(fp,inoaddr,SEEK_SET);
	fread(&dirinode,neo_sb_info.s_inode_size,1,fp);
	if (dirinode.i_blocks == 0) {
		goto sd_out;
	}
	blkcnt = dirinode.i_blocks;
	if (blkcnt <= IN_INDEX_BGN) {
		n = blkcnt;
	} else {
		n = IN_INDEX_BGN;
	}
	for (i = 0; i < n; i++) {
		blkaddr = block_to_addr(dirinode.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0) {
			res = info[0];
			goto sd_out;
		}
	}
	if (blkcnt > IN_INDEX_BGN) {		/*if blocks count >= 13,block[12] for indirect addr.*/
		n = blkcnt - IN_INDEX_BGN;
		p = (__u32 *)malloc(4 * n);	/*4 = sizeof(__32)*/
		fseek(fp,block_to_addr(dirinode.i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++) {
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0) {
				res = info[0];
				goto sd_out;
			}
		}
	}
sd_out:
	free(p);
	return res;
}

/*
 * add_dentry - after get inode successfully,add dentry in the parent dir
 * @parent_ino: parent dir's inode
 * @ino: the new file's inode
 * @name: new file's filename
 * @i_mode: new file's filetype and authority
 */
int add_dentry(inode_nr parent_ino,inode_nr ino,char * name,__u16 i_mode)
{
	int blkcnt;
	int i,n;
	__u32 *p = NULL;
	__u64 blkaddr;
	unsigned int info[4] = {0};
	unsigned int tmp;
	struct neo_inode parent;
	struct neo_dir_entry dirent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);	/* read parent dir's inode */

	blkcnt = parent.i_blocks;
	if (blkcnt <= IN_INDEX_BGN) {
		n = blkcnt;
	} else {
		n = IN_INDEX_BGN;
	}
	/* first，check all dentries to ensure there's no same-name file */
	for (i = 0; i < n; i++) {
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0) {
			printf("same name\n");
			errno = EEXIST;
			goto ad_err_out;
		}
	}
	if (blkcnt > IN_INDEX_BGN) {	/* dir file's max blocks count is 13,block[12] for indirect addr */
		n = blkcnt - IN_INDEX_BGN;
		p = (__u32 *)malloc(4 * n);
		fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++) {
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0) {
				printf("same name\n");
				errno = EEXIST;
				goto ad_err_out;
			}
		}
	}

	/* then，find empty location to store the dentry in the existing blocks */
	if (blkcnt <= IN_INDEX_BGN) {
		n = blkcnt;
	} else {
		n = IN_INDEX_BGN;
	}
	memset(dirent.name,0,MAX_FILE_NAME);
	dirent.inode = ino;
	dirent.name_len = strlen(name);
	strcpy(dirent.name,name);
	dirent.file_type = (__u8)i_mode;
	
	if (parent.i_blocks == 0){
		parent.i_block[0] = get_block(parent_ino);
		parent.i_blocks += 1;
		dirent.rec_len = BLOCK_SIZE;			/* the first dentry and meanwhile the last dentry */
		fseek(fp,block_to_addr(parent.i_block[0]),SEEK_SET);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		goto ad_out;
	}

	for (i = 0; i < n; i++) {
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_empty_dentry(blkaddr,name,info) == 0) {	/* find the empty location */
			write_dentry(blkaddr,info,dirent);
			goto ad_out;
		}
	
	}
	if (blkcnt > IN_INDEX_BGN) {	/* dir file's max blocks count is 13,block[12] for indirect addr */
		n = blkcnt - IN_INDEX_BGN;
		fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++) {
			blkaddr = block_to_addr(p[i]);
			if (blk_search_empty_dentry(blkaddr,name,info) == 0) {
				write_dentry(blkaddr,info,dirent);
				goto ad_out;
			}
		}
	}

	/* if still not allocated，ie,all the existing blocks are full，allocate one more */
	if (blkcnt < IN_INDEX_BGN) {
		parent.i_block[blkcnt] = get_block(parent_ino);
		parent.i_blocks += 1;
		dirent.rec_len = BLOCK_SIZE;		/* the first dentry and meanwhile the last */
		fseek(fp,block_to_addr(parent.i_block[blkcnt]),SEEK_SET);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		goto ad_out;
	} else if (blkcnt == IN_INDEX_BGN) {
		parent.i_block[IN_INDEX_BGN] = get_block(parent_ino);
		tmp = get_block(parent_ino);
		parent.i_blocks += 1;
		fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]),SEEK_SET);
		fwrite(&tmp,4,1,fp);
		dirent.rec_len = BLOCK_SIZE;		/* the first dentry and meanwhile the last */
		fseek(fp,block_to_addr(tmp),SEEK_SET);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		goto ad_out;
	} else if (blkcnt < ININ_INDEX_BGN) {		/* 1036 = 4096/4 + 12，ie,dir file's max blocks count */
		tmp = get_block(parent_ino);
		parent.i_blocks += 1;
		fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]) + (blkcnt - IN_INDEX_BGN) * 4,SEEK_SET);
		fwrite(&tmp,4,1,fp);
		dirent.rec_len = BLOCK_SIZE;		/* the first dentry and meanwhile the last */
		fseek(fp,block_to_addr(tmp),SEEK_SET);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		goto ad_out;
	} else {
		errno = ENOSPC;
		goto ad_err_out;
	}
ad_err_out:
	free(p);
	return -1;
ad_out:
	parent.i_mtime = time(NULL);
	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fwrite(&parent,sizeof(struct neo_inode),1,fp);	/* write back parent dir's inode */
	free(p);
	return 0;
}

/*
 * delete_dentry - when delete a file,delete its dir entry from its parent dir
 * @parent_ino: deleting file's parent dir's inode
 * @name: deleting file's name
 * @i_mode: deleting file's type and authority
 */
int delete_dentry(inode_nr parent_ino,char *name,__u16 i_mode)
{
	int blkcnt;
	int i,n;
	__u32 *p = NULL;
	__u64 blkaddr;
	unsigned int info[4] = {0};
	struct neo_inode parent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);	/*read parent dir's inode*/

	blkcnt = parent.i_blocks;
	if (blkcnt <= IN_INDEX_BGN) {
		n = blkcnt;
	} else {
		n = IN_INDEX_BGN;
	}
	for (i = 0; i < n; i++) {
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0) {
			delete_block_dentry(parent_ino,i,blkaddr,info);
			return 0;
		}
	}
	if (blkcnt > IN_INDEX_BGN) {
		n = blkcnt - IN_INDEX_BGN;
		p = (__u32 *)malloc(4 * n);
		fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++) {
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0) {
				delete_block_dentry(parent_ino,(i + IN_INDEX_BGN),blkaddr,info);
				free(p);
				return 0;
			}
		}
	}
	errno = ENOENT;
	parent.i_mtime = time(NULL);
	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*write back parent dir's inode*/
	free(p);
	return -1;
}

/*
 * delete_block_dentry - delete dir entry in parent dir's targeted block
 * @parent_ino: parent dir's inode
 * @blknr: the parent dir's data block which contains the deleting file's dentry
 * @blkaddr: the block's address
 * @info: information about the deleting dentry
 *	info[0] is deleting file's inode
 *	info[1] is deleting dentry's rec_len
 *	info[2] is deleting dentry's prev dentry's offset in the block
 *	info[3] is deleting dentry's offset in the block
 */
void delete_block_dentry(inode_nr parent_ino,int blknr,__u64 blkaddr,unsigned int info[])
{
	block_nr tmpnr;
	unsigned short true_prev_len,new_blanklen = 0;
	struct neo_dir_entry prev;
	struct neo_dir_entry del;
	struct neo_inode parent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);
	fseek(fp,(blkaddr + info[2]),SEEK_SET);
	fread(&prev,8,1,fp);
	fseek(fp,(blkaddr + info[3]),SEEK_SET);
	fread(&del,8,1,fp);
	true_prev_len = TRUE_LEN(prev.name_len);

	if ((info[2] == info[3]) && ((info[1] + info[3]) == 4096)) {	/* last dentry in this block */
		if (blknr == (parent.i_blocks - 1)) {	/* last block of this inode */
			if (blknr <= DIRECT_INDEX_END) {
				free_block(parent.i_block[blknr]);
				parent.i_block[blknr] = 0;
			} else if (blknr == IN_INDEX_BGN) {
				fseek(fp,block_to_addr(parent.i_block[IN_INDEX_BGN]),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
				free_block(parent.i_block[IN_INDEX_BGN]);
				parent.i_block[IN_INDEX_BGN] = 0;
			} else {
				fseek(fp,(block_to_addr(parent.i_block[IN_INDEX_BGN]) + (blknr - IN_INDEX_BGN) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
			}
		} else {				/* not the last block of this inode */
			if (blknr <= DIRECT_INDEX_END) {
				free_block(parent.i_block[blknr]);
				if (parent.i_blocks <= IN_INDEX_BGN) {
					tmpnr = parent.i_block[parent.i_blocks - 1];
				} else {
					fseek(fp,(block_to_addr(parent.i_block[IN_INDEX_BGN]) + (parent.i_blocks - 1 - IN_INDEX_BGN) * 4),SEEK_SET);
					fread(&tmpnr,4,1,fp);
				}
				if ((parent.i_blocks - 1 - IN_INDEX_BGN) == 0) {
					free_block(parent.i_block[IN_INDEX_BGN]);
				}
				parent.i_block[blknr] = tmpnr;
			} else {
				fseek(fp,(block_to_addr(parent.i_block[IN_INDEX_BGN]) + (blknr - IN_INDEX_BGN) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
				fseek(fp,(block_to_addr(parent.i_block[IN_INDEX_BGN]) + (parent.i_blocks - 1 - IN_INDEX_BGN) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				fseek(fp,(block_to_addr(parent.i_block[IN_INDEX_BGN]) + (blknr - IN_INDEX_BGN) * 4),SEEK_SET);
				fwrite(&tmpnr,4,1,fp);
				if ((parent.i_blocks - 1 - IN_INDEX_BGN) == 0) {
					free_block(parent.i_block[IN_INDEX_BGN]);
				}
			}
		}
		parent.i_blocks--;
		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);
	} else if ((info[2] == info[3]) && (info[2] == 0)) {	/* the first dentry of the block,zero offset */
		del.inode = 0;
		fseek(fp,blkaddr,SEEK_SET);
		fwrite(&del,8,1,fp);
	} else if ((info[2] == info[3]) && (info[2] != 0)) {	/* the first dentry of the block,non-zero offset */
		new_blanklen = info[2] + del.rec_len;
		fseek(fp,blkaddr + 4,SEEK_SET);
		fwrite(&new_blanklen,2,1,fp);
	} else if (prev.rec_len > true_prev_len) {		/* not the first dentry,after blank */
		prev.rec_len += del.rec_len;
		fseek(fp,(blkaddr + info[2]),SEEK_SET);
		fwrite(&prev,8,1,fp);
		new_blanklen = info[3] - info[2] -true_prev_len + del.rec_len;
		fseek(fp,(blkaddr + info[2] + true_prev_len + 4),SEEK_SET);
		fwrite(&new_blanklen,2,1,fp);
	} else if (prev.rec_len == true_prev_len) {		/* not the first dentry,after dentry */
		prev.rec_len += del.rec_len;
		fseek(fp,(blkaddr + info[2]),SEEK_SET);
		fwrite(&prev,8,1,fp);
		del.inode = 0;
		fseek(fp,(blkaddr + info[3]),SEEK_SET);
		fwrite(&del,8,1,fp);
	}
}

/*
 * blk_search_dentry - find the dentry whose file name is *name in the selected block
 * @blkaddr: selected block's address
 * @name: searching file's name
 * @info: same with above
 */
int blk_search_dentry(__u64 blkaddr,char *name,unsigned int info[])
{

	unsigned int offset_prev = 0;		/* previous dentry's offset */
	unsigned int offset_cur = 0;		/* current dentry's offset */
	struct neo_dir_entry *cur;		/* current dentry */
	char cname[MAX_FILE_NAME] = {'\0'};
	void *origin,*block;			/* origin for free,block for move pointer */
						/* only for gnu c,void * can do calculation only in gcc，not for ansi C */
						/* block for pointer movement，then cur = block */

	origin = (void *)malloc(BLOCK_SIZE);
	block = origin;
	cur = origin;
	fseek(fp,blkaddr,SEEK_SET);
	fread(origin,BLOCK_SIZE,1,fp);		/* read this block into memory */

	if(cur->inode == 0) {			/* beginning is a blank,then make block points to the first dentry */
		block += cur->rec_len;
		offset_prev += cur->rec_len;
		offset_cur += cur->rec_len;
	}
	do {					/* when cur is not the last dentry */
		cur = block;
		strncpy(cname,cur->name,cur->name_len);
		if (strcmp(cname,name) == 0) {
			info[0] = cur->inode;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			free(origin);
			return 0;
		}
		offset_prev = offset_cur;
		offset_cur += cur->rec_len;
		block += cur->rec_len;
		memset(cname,0,MAX_FILE_NAME);
	} while ((offset_prev + cur->rec_len) != 4096 );
	free(origin);
	return -1;
}

/*
 * blk_search_empty_dentry - find empty location in parent dir's block at blkaddr
 * @blkaddr: searching block's address
 * @name: to calculate the length neened by the new dentry
 * @info: information about the blank location in the block
 * 	info[0] is the order of the dentry in front of the blank we're looking for 
 * 	info[1] is the rec_len of the dentry in front of the blank
 * 	info[2] is the offset of the dentry before the dentry in front of the blank
 * 	info[3] is the offset of the dentry in front of the blank
 */
int blk_search_empty_dentry(__u64 blkaddr,char *name,unsigned int info[])
{

	unsigned int offset_prev = 0;		/* previous dentry's offset */
	unsigned int offset_cur = 0;		/* current dentry's offset */
	unsigned int order = 0;		
	struct neo_dir_entry *cur;		/* current dentry */
	unsigned char need_len;
	unsigned short true_len;
	void *origin,*block;			/* same with blk_search_dentry */
	need_len = TRUE_LEN(strlen(name)) + 8;

	origin = (void *)malloc(BLOCK_SIZE);
	block = origin;
	cur = origin;
	fseek(fp,blkaddr,SEEK_SET);
	fread(origin,BLOCK_SIZE,1,fp);		/* read the block into memory */

	if(cur->inode == 0) {			/* beginning is blank,then make block point to the first dentry */
		if (cur->rec_len >= need_len) {
			info[0] = order;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			free(origin);
			return 0;
		} else {
			offset_prev += cur->rec_len;
			offset_cur += cur->rec_len;
			block += cur->rec_len;
		}
	}
	do {					/* when cur is not the last dentry */
		order++;
		cur = block;
		true_len = TRUE_LEN(cur->name_len);
		if ((cur->rec_len - true_len) > need_len) {
			info[0] = order;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			free(origin);
			return 0;
		}
		offset_prev = offset_cur;
		offset_cur += cur->rec_len;
		block += cur->rec_len;
	} while ((offset_prev + cur->rec_len) != 4096 );
	free(origin);
	return -1;
}

/*
 * write_dentry - after get the blank location in the block,write dentry in
 * @blkaddr: address of the block in which the new dentry is written in
 * @info: same with above
 * @dirent: the dentry to be written
 */
void write_dentry(__u64 blkaddr,unsigned int info[],struct neo_dir_entry dirent)
{
	unsigned short true_len;
	unsigned short mid;
	struct neo_dir_entry blank;
	struct neo_dir_entry tmp;
	blank.inode = 0;
	blank.name_len = 0;
	blank.file_type = 0;
	true_len = TRUE_LEN(dirent.name_len);
	if (info[0] == 0) {				/* blank at the beginning of the block */
		dirent.rec_len = info[1];
		fseek(fp,blkaddr,SEEK_SET);
		fwrite(&dirent,true_len,1,fp);
		blank.rec_len = info[1] - true_len;
		fwrite(&blank,8,1,fp);
	} else if ((info[1] + info[3]) == BLOCK_SIZE) {	/* blank at the end of the block */
		fseek(fp,blkaddr + info[3],SEEK_SET);
		fread(&tmp,8,1,fp);
		tmp.rec_len = TRUE_LEN(tmp.name_len);
		fseek(fp,-8,SEEK_CUR);
		fwrite(&tmp,8,1,fp);

		dirent.rec_len = 4096 - info[3] - tmp.rec_len;
		fseek(fp,blkaddr + info[3] + tmp.rec_len,SEEK_SET);
		fwrite(&dirent,true_len,1,fp);

		blank.rec_len = dirent.rec_len - true_len;
		fwrite(&blank,8,1,fp);
	} else {					/* blank in the middle */
		fseek(fp,blkaddr + info[3],SEEK_SET);
		fread(&tmp,8,1,fp);
		mid = tmp.rec_len;
		tmp.rec_len = TRUE_LEN(tmp.name_len);
		fseek(fp,-8,SEEK_CUR);
		fwrite(&tmp,8,1,fp);

		dirent.rec_len = mid - tmp.rec_len;
		fseek(fp,blkaddr + info[3] + tmp.rec_len,SEEK_SET);
		fwrite(&dirent,true_len,1,fp);

		blank.rec_len = dirent.rec_len - true_len;
		fwrite(&blank,8,1,fp);
	}
}

/*
 * get_inode - allocate an inode when create a file
 * @ino: parent dir's inode,try to allocate in the same group
 * @i_mode: filetype of the new file
 */
inode_nr get_inode(inode_nr ino,__u16 i_mode)
{
	int i,j;
	unsigned char c;
	unsigned int aver_free_inodes,aver_free_blocks;
	inode_nr res;
	__u32 groupcnt = neo_sb_info.s_groups_count;
	int bgnr = ino / INODES_PER_GROUP;
	int prev,tag = 0;
	if (i_mode == 1) {				/* get inode for reg file */
		for (i = 0; i < groupcnt; i++) {
			if (neo_gdt[bgnr].bg_free_inodes_count > 0) {
				neo_sb_info.s_free_inodes_count--;
				neo_gdt[bgnr].bg_free_inodes_count--;
				write_sb_gdt_main(bgnr);
				break;
			}
			bgnr = (bgnr + 1)%groupcnt;
		}
	} else {					/* get inode for dir file */
		aver_free_inodes = neo_sb_info.s_free_inodes_count / neo_sb_info.s_groups_count;
		aver_free_blocks = neo_sb_info.s_free_blocks_count / neo_sb_info.s_groups_count;
		for (i = 0; i < groupcnt; i++) {
			prev = (bgnr - 1 + groupcnt)%groupcnt;
			if ((neo_gdt[bgnr].bg_free_inodes_count > 0) && \
				(neo_gdt[bgnr].bg_used_dirs_count < neo_gdt[prev].bg_used_dirs_count) && \
				(neo_gdt[bgnr].bg_free_inodes_count > aver_free_inodes) && \
				(neo_gdt[bgnr].bg_free_blocks_count > aver_free_blocks)){
				neo_sb_info.s_free_inodes_count--;
				neo_gdt[bgnr].bg_free_inodes_count--;
				neo_gdt[bgnr].bg_used_dirs_count++;
				write_sb_gdt_main(bgnr);
				tag = 1;
				break;
			}
			bgnr = (bgnr + 1)%groupcnt;
		}/* not find the optimal group,make a linar search from the current group */
		if (tag == 0) {
			for (i = 0; i < groupcnt; i++) {
				if (neo_gdt[bgnr].bg_free_inodes_count > 0) {
					neo_sb_info.s_free_inodes_count--;
					neo_gdt[bgnr].bg_free_inodes_count--;
					neo_gdt[bgnr].bg_used_dirs_count++;
					write_sb_gdt_main(bgnr);
					break;
				}
				bgnr = (bgnr + 1)%groupcnt;
			}
		}
	}
	if (ibcache.groupnr != bgnr) {
		if (ibcache.groupnr != -1) {
			fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
			fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_inode_bitmap),SEEK_SET);
		fread(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		ibcache.groupnr = bgnr;
		ibcache.lastzero = 0;
	}
iretry:	for (i = ibcache.lastzero; i < (BLOCK_SIZE / 4); i++) {
		if (ibcache.ibitmap[i] != 0xFF) {
			for (j = 0, c = 0x80; j < 8; j++) {
				if ((ibcache.ibitmap[i]&c) == 0) {
					ibcache.ibitmap[i] += c;
					ibcache.lastzero = i;
					res = neo_sb_info.s_inodes_per_group * bgnr + 8 * i + j;
					init_inode(res,i_mode);
					return res;
				}
				c = c >> 1;
			}
		}
	}
	ibcache.lastzero = 0;
	goto iretry;
}

/*
 * init_inode - initial the new file's inode
 * @res: the new file's inode
 * @i_mode: new file's type and authority
 */
void init_inode(inode_nr res,__u16 i_mode)
{
	__u64 addr;
	addr = inode_to_addr(res);
	struct neo_inode new_inode;
	new_inode.i_uid = getuid();
	new_inode.i_gid = getgid();
	new_inode.i_size = 0;
	new_inode.i_blocks = 0;
	new_inode.i_atime = time(NULL);
	new_inode.i_ctime = new_inode.i_atime;
	new_inode.i_mtime = new_inode.i_atime;
	memset(new_inode.i_block,0,NEO_BLOCKS * sizeof(__u32));
	new_inode.i_mode = i_mode;
	fseek(fp,addr,SEEK_SET);
	fwrite(&new_inode,sizeof(struct neo_inode),1,fp);
}

/*
 * get_block - allocate block when needed
 * @ino: try to allocate blocks in the same group within a file
 */
block_nr get_block(inode_nr ino)
{
	int i,j;
	block_nr res;
	unsigned char c;
	char *zero = NULL;	/* before allocation,set the block all 0 */
	__u32 groupcnt = neo_sb_info.s_groups_count;
	bg_nr bgnr = ino / INODES_PER_GROUP;
	for (i = 0; i < groupcnt; i++) {
		if (neo_gdt[bgnr].bg_free_blocks_count > 0) {
			neo_sb_info.s_free_blocks_count--;
			neo_gdt[bgnr].bg_free_blocks_count--;
			write_sb_gdt_main(bgnr);
			break;
		}
		bgnr = (bgnr + 1)%groupcnt;
	}
	if (bbcache.groupnr != bgnr) {
		if (bbcache.groupnr != -1) {
			fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
			fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_block_bitmap),SEEK_SET);
		fread(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		bbcache.groupnr = bgnr;
		bbcache.lastzero = FIRST_FREE_BLOCK;
	}
retry:	for (i = bbcache.lastzero; i < BLOCK_SIZE; i++) {	/* 32 is the first 256 + 2or4 used blocks in the bitmap */
		if (bbcache.bbitmap[i] != 0xFF){		/* find empty block */
			for (j = 0, c = 0x80; j < 8; j++){
				if ((bbcache.bbitmap[i]&c) == 0){
					bbcache.bbitmap[i] += c;
					bbcache.lastzero = i;
					res = BLOCKS_PER_GROUP * bgnr + 8 * i + j;
					// /*
					zero = malloc(BLOCK_SIZE);
					memset(zero,0,BLOCK_SIZE);
					fseek(fp, block_to_addr(res), SEEK_SET);
					fwrite(zero,1,BLOCK_SIZE,fp);
					free(zero);
					// */
					return res;
				}
				c = c >> 1;
			}
		}
	}
	bbcache.lastzero = FIRST_FREE_BLOCK;
	goto retry;
}

/*
 * free_inode - free the inode when deleting a file
 * @ino: file's inode
 */
int free_inode(inode_nr ino)
{
	bg_nr bgnr = ino / INODES_PER_GROUP;
	__u16 i_mode;
	int l = (ino % INODES_PER_GROUP) / 8;
	int r = (ino % INODES_PER_GROUP) % 8;
	__u64 addr;
	unsigned char c = 0x80;
	struct neo_inode del_inode;
	addr = inode_to_addr(ino);
	fseek(fp,addr,SEEK_SET);
	fread(&del_inode,1,sizeof(struct neo_inode),fp);
	i_mode = del_inode.i_mode;
	if ((i_mode == 2) && (del_inode.i_blocks != 0)) {
		errno = ENOTEMPTY;
		return -1;
	}
	if (ibcache.groupnr != bgnr) {
		if (ibcache.groupnr != -1) {
			fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
			fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_inode_bitmap),SEEK_SET);
		fread(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		ibcache.groupnr = bgnr;
	}
	ibcache.ibitmap[l] -= (c >> r);
	if ((i_mode == 1) && (del_inode.i_blocks != 0)) {
		free_selected_blocks(del_inode.i_block,0,(del_inode.i_blocks - 1));
	}
	neo_sb_info.s_free_inodes_count++;
	neo_gdt[bgnr].bg_free_inodes_count++;
	if (i_mode == 2) {
		neo_gdt[bgnr].bg_used_dirs_count--;
	}
	write_sb_gdt_main(bgnr);

	return 0;
}

/*
 * get_selected_blocks - allocate appointed blocks for file
 * @i_block: file inode's i_block[] array which contains the block index
 * @ino: file's inode
 * @start,end: appointed blocks of the file,from 0 to i_blocks - 1
 */
void get_selected_blocks(__u32 *i_block,inode_nr ino,__u32 start,__u32 end)
{
	int i;
	int blk_n,blk_r;
	block_nr blknr,iblknr;
	__u64 iaddr;
	/* direct index */
	if (end <= DIRECT_INDEX_END) {
		for (i = start; i <= end; i++) {
			i_block[i] = get_block(ino);
		}
	/* in-direct index */
	} else if (end <= IN_INDEX_END) {
		if (start <= IN_INDEX_BGN) {
			i_block[12] = get_block(ino);
		}
		for (i = start; i <= DIRECT_INDEX_END; i++) {
			i_block[i] = get_block(ino);
		}
		iaddr = block_to_addr(i_block[IN_INDEX_BGN]);
		for (i = ((start > IN_INDEX_BGN) ? start : IN_INDEX_BGN); i <= end; i++) {
			blknr = get_block(ino);
			fseek(fp,iaddr + (i - IN_INDEX_BGN) * 4,SEEK_SET);
			fwrite(&blknr,4,1,fp);
		}
	/* in-in-direct index */
	} else {
		if (start <= IN_INDEX_BGN) {
			i_block[12] = get_block(ino);
		}
		for (i = start; i <= DIRECT_INDEX_END; i++) {
			i_block[i] = get_block(ino);
		}

		if (start <= ININ_INDEX_BGN) {
			i_block[13] = get_block(ino);
		}
		for (i = ((start > IN_INDEX_BGN) ? start : IN_INDEX_BGN); i <= IN_INDEX_END; i++) {
			blknr = get_block(ino);
			fseek(fp,(block_to_addr(i_block[IN_INDEX_BGN]) + (i - IN_INDEX_BGN) * 4),SEEK_SET);
			fwrite(&blknr,4,1,fp);
		}
		for (i = ((start > ININ_INDEX_BGN) ? start : ININ_INDEX_BGN); i <= end; i++) {
			blk_n = (i - ININ_INDEX_BGN) / 1024;
			blk_r = (i - ININ_INDEX_BGN) % 1024;
			if (blk_r == 0) {
				iblknr = get_block(ino);
				fseek(fp,(block_to_addr(i_block[13]) + blk_n * 4),SEEK_SET);
				fwrite(&iblknr,4,1,fp);
				blknr = get_block(ino);
				fseek(fp,block_to_addr(iblknr),SEEK_SET);
				fwrite(&blknr,4,1,fp);
			} else {
				fseek(fp,(block_to_addr(i_block[13]) + blk_n * 4),SEEK_SET);
				fread(&iblknr,4,1,fp);
				blknr = get_block(ino);
				fseek(fp,(block_to_addr(iblknr) + blk_r * 4),SEEK_SET);
				fwrite(&blknr,4,1,fp);
			}
		}
	}
}

/*
 * free_selected_blocks - opposite with above,free appointed blocks of a file
 * direct index，1-indirect index，2-indirect index block number:0~11,12~1035,1036~1049611
 * @i_block: file's block index array
 * @start,end: appointed blocks
 */
void free_selected_blocks(__u32 *i_block,__u32 start,__u32 end)
{
	int i,j;
	int begin_page,end_page,begin_remainder,end_remainder;
	void *block = NULL;
	void *sub_block = NULL;
	__u32 *p = NULL;
	__u32 *q = NULL;
	/* direct index */
	if (end <= DIRECT_INDEX_END) {
		for (i = start; i <= end; i++) {
			free_block(i_block[i]);
		}
	/* in-direct index */
	} else if (end <= IN_INDEX_END) {
		for (i = start; i <= DIRECT_INDEX_END; i++) {
			free_block(i_block[i]);
		}
		block = (void *)malloc(BLOCK_SIZE);
		p = block;
		fseek(fp,block_to_addr(i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		for (i = ((start > DIRECT_INDEX_END) ? start : IN_INDEX_BGN) - IN_INDEX_BGN; i <= end -IN_INDEX_BGN; i++) {
			free_block(*(p + i));
		}
		if (start <= IN_INDEX_BGN) {
			free_block(i_block[IN_INDEX_BGN]);
		}
	/* in-in-direct index */
	} else {
		for (i = start; i <= DIRECT_INDEX_END; i++) {
			free_block(i_block[i]);
		}
		block = (void *)malloc(BLOCK_SIZE);
		p = block;
		sub_block = (void *)malloc(BLOCK_SIZE);
		q = sub_block;
		fseek(fp,block_to_addr(i_block[IN_INDEX_BGN]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		for (i = ((start > DIRECT_INDEX_END) ? start : IN_INDEX_BGN) - IN_INDEX_BGN; i <= 1023; i++) {
			free_block(*(p + i));
		}

		fseek(fp,block_to_addr(i_block[13]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		begin_page = (((start > IN_INDEX_END) ? start : ININ_INDEX_BGN) - ININ_INDEX_BGN) / 1024;
		begin_remainder = (((start > IN_INDEX_END) ? start : ININ_INDEX_BGN) - ININ_INDEX_BGN) % 1024;
		end_page = (end - ININ_INDEX_BGN) / 1024;
		end_remainder = (end - ININ_INDEX_BGN) % 1024;
		if (begin_page == end_page) {
			fseek(fp,block_to_addr(*(p + begin_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = begin_remainder; i <= end_remainder; i++) {
				free_block(*(q + i));
			}
			if (begin_remainder == 0) {
				free_block(*(p + begin_page));
			}
		} else {
			fseek(fp,block_to_addr(*(p + begin_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = begin_remainder; i <= 1023; i++) {
				free_block(*(q + i));
			}
			if (begin_remainder == 0) {
				free_block(*(p + begin_page));
			}

			for (i = begin_page + 1; i <= end_page - 1; i++) {
				fseek(fp,block_to_addr(*(p + i)),SEEK_SET);
				fread(sub_block,BLOCK_SIZE,1,fp);
				for (j = 0; j <= 1023; j++) {
					free_block(*(q + j));
				}
				free_block(*(p + i));
			}

			fseek(fp,block_to_addr(*(p + end_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = 0; i <= end_remainder; i++) {
				free_block(*(q + i));
			}
			free_block(*(p + end_page));
		}
		if (start <= IN_INDEX_BGN) {
			free_block(i_block[IN_INDEX_BGN]);
			free_block(i_block[13]);
		} else if (start <= ININ_INDEX_BGN) {
			free_block(i_block[13]);
		}
	}
	free(block);
	free(sub_block);
}

/*
 * free_block - free one block
 * @blk: number of the block to be freed
 */
void free_block(block_nr blk)
{
	bg_nr bgnr = blk / BLOCKS_PER_GROUP;
	int l = (blk % BLOCKS_PER_GROUP) / 8;
	int r = (blk % BLOCKS_PER_GROUP) % 8;
	unsigned char c = 0x80;
	if (bbcache.groupnr != bgnr) {
		if (bbcache.groupnr != -1) {
			fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
			fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_block_bitmap),SEEK_SET);
		fread(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		bbcache.groupnr = bgnr;
	}
	bbcache.bbitmap[l] -= (c >> r);
	neo_sb_info.s_free_blocks_count++;
	neo_gdt[bgnr].bg_free_blocks_count++;
	write_sb_gdt_main(bgnr);
}

/*
 * write_sb_gdt_main - when meta data is changed sb&gdt need to be written
 * @bgnr: the changed block group's number
 */
void write_sb_gdt_main(bg_nr bgnr)
{
	fseek(fp,1024,SEEK_SET);
	fwrite(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	fseek(fp,4096 + (bgnr * sizeof(struct neo_group_desc)),SEEK_SET);
	fwrite((neo_gdt + bgnr),sizeof(struct neo_group_desc),1,fp);
}

void write_sb_gdt_backups()
{
}

void write_bitmap()
{
	if (bbcache.groupnr != -1) {
		fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
		fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
	}
	if (ibcache.groupnr != -1) {
		fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
		fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
	}
}

void print_sb(struct neo_super_block neo_sb_info)
{
	printf("super block:\n");
	printf("sb inodes count: %u\n",neo_sb_info.s_inodes_count);
	printf("sb blocks count: %u\n",neo_sb_info.s_blocks_count);
	printf("sb groups count: %u\n",neo_sb_info.s_groups_count);
	printf("sb free inodes count: %u\n",neo_sb_info.s_free_inodes_count);
	printf("sb free blocks count: %u\n",neo_sb_info.s_free_blocks_count);
	printf("sb log block size: %u\n",neo_sb_info.s_log_block_size);
	printf("sb blocks/group: %u\n",neo_sb_info.s_blocks_per_group);
	printf("sb inodes/group: %u\n",neo_sb_info.s_inodes_per_group);
	printf("sb magic#: %d\n",neo_sb_info.s_magic);
	printf("sb inode size: %d\n\n",neo_sb_info.s_inode_size);
}

void print_gdt(struct neo_group_desc *gdt,int groupcnt)
{
	int i;
	printf("GDT:\n");
	for (i = 0; i < groupcnt; i++) {
		printf("group %d :\n",i);
		printf("block bitmap: %d\n",gdt[i].bg_block_bitmap);
		printf("inode bitmap: %d\n",gdt[i].bg_inode_bitmap);
		printf("inode table: %d\n",gdt[i].bg_inode_table);
		printf("free blocks count: %d\n",gdt[i].bg_free_blocks_count);
		printf("free inodes count: %d\n",gdt[i].bg_free_inodes_count);
		printf("used dirs count: %d\n\n",gdt[i].bg_used_dirs_count);
	}
}

void print_inode(struct neo_inode ino)
{
	struct tm *local_time = NULL;
	char str_time[100];
	printf("inode info:\n");
	printf("uid: %d\n",ino.i_uid);
	printf("gid: %d\n",ino.i_gid);
	printf("size: %d\n",ino.i_size);
	printf("blocks: %d\n",ino.i_blocks);
	printf("block[0]: %d\n",ino.i_block[0]);

	local_time = localtime(&ino.i_atime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last access time:   ");
	printf(" %-19s\n",str_time);
	local_time = localtime(&ino.i_ctime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("created time:       ");
	printf(" %-19s\n",str_time);
	local_time = localtime(&ino.i_mtime);
	strftime(str_time, sizeof(str_time), "%Y-%m-%d,%H:%M:%S", local_time);
	printf("last modified time: ");
	printf(" %-19s\n",str_time);

	printf("mode: %d\n",ino.i_mode);
}

__u64 inode_to_addr(inode_nr ino)
{
	__u32 groupnr,r;
	__u64 offset = (__u64)BLOCK_SIZE * 2;			/*block and inode's bitmaps*/
	groupnr = ino / neo_sb_info.s_inodes_per_group;
	r = ino % neo_sb_info.s_inodes_per_group;
	if(is_powerof_357(groupnr))
		offset += BLOCK_SIZE * 2;		/*SB&GDT 2Blocks*/
	offset += (__u64)BLOCK_SIZE * BLOCKS_PER_GROUP * groupnr + (__u64)r * neo_sb_info.s_inode_size;
	return offset;
}

__u64 i_block_to_addr(block_nr blknr,block_nr i_block[])
{
	__u64 blkaddr;
	__u32 iiblk_n,iiblk_r,blk,iblk;
	if (blknr <= DIRECT_INDEX_END) {
		blkaddr = block_to_addr(i_block[blknr]);
	} else if (blknr <= IN_INDEX_END) {
		fseek(fp,((__u64)block_to_addr(i_block[IN_INDEX_BGN]) + ((__u64)(blknr - IN_INDEX_BGN) * 4)),SEEK_SET);
		fread(&blk,4,1,fp);
		blkaddr = block_to_addr(blk);
	} else {
		iiblk_n = (blknr - ININ_INDEX_BGN) / 1024;
		iiblk_r = (blknr - ININ_INDEX_BGN) % 1024;
		fseek(fp,(block_to_addr(i_block[13]) + iiblk_n * 4),SEEK_SET);
		fread(&iblk,4,1,fp);
		fseek(fp,(block_to_addr(iblk) + iiblk_r * 4),SEEK_SET);
		fread(&blk,4,1,fp);
		blkaddr = block_to_addr(blk);
	}
	return blkaddr;
}

__u64 inline block_to_addr(block_nr blk)
{
	__u64 res = (__u64)blk * (__u64)4096;
	return res;
}

int is_powerof_357(int i)
{
	if (pow(3,(int)(float)(log(i)/log(3))) == i || pow(5,(int)(float)(log(i)/log(5))) == i || 
		pow(7,(int)(float)(log(i)/log(7))) == i || i == 0) {
		return 1;
	}
	return 0;
}









