#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "atomic_ops.h"
#include "neo_fs.h"
#include <errno.h>
#include <time.h>
#include <math.h>
#include <syslog.h>

inode_nr path_resolve(char *path)
{
	inode_nr parent,res;
	int length;
	char *pos;
	char tmp[MAX_FILE_NAME];
	if (strcmp(path,"/") == 0)//root's inode is 1. 0 is reserved for judge
		return 1;
	path ++;
	parent = 1;
	while ((pos = strchr(path,'/')) != NULL)
	{
		memset(tmp,0,MAX_FILE_NAME);
		if ((length = (pos - path)) > 255){
			errno = ENAMETOOLONG;
			return 0;
		}
		strncpy(tmp,path,pos - path);
		//syslog(LOG_INFO,"path resolve while %s",tmp);
		parent = search_dentry(parent,tmp);
		//syslog(LOG_INFO,"path resolve while %d",parent);
		//printf("%s\n",tmp);
		if (parent == 0){
			errno = ENOENT;
			return 0;
		}
		path = ++pos;
	}
	//printf("%s\n",path);
	//syslog(LOG_INFO,"path resolve last %d",parent);
	//syslog(LOG_INFO,"path resolve last %s",path);
	if ((res = search_dentry(parent,path)) != 0){
		//syslog(LOG_INFO,"path resolve res %u",res);
		return res;
	}else {
		errno = ENOENT;
		return 0;
	}
}

inode_nr search_dentry(inode_nr ino, char *name)
{
	unsigned int blkcnt;
	unsigned int info[4] = {0};
	__u64 inoaddr;
	__u64 blkaddr;
	block_nr *p = NULL;
	struct neo_inode dirinode;
	int i,n;
	inoaddr = inode_to_addr(ino);
	fseek(fp,inoaddr,SEEK_SET);
	fread(&dirinode,neo_sb_info.s_inode_size,1,fp);
	//print_inode(dirinode);
	if (dirinode.i_blocks == 0)
		return 0;
	blkcnt = dirinode.i_blocks;
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	//printf("blkcnt : %d",blkcnt);
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(dirinode.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0)
			return info[0];
	}
	if (blkcnt > 12){//dir file's max blocks count is 13,block[12] for indirect addr.
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);	//4 = sizeof(__32)
		fseek(fp,block_to_addr(dirinode.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0){
				free(p);
				return info[0];
			}
		}
		free(p);
	}
	return 0;
}

int add_dentry(inode_nr parent_ino,inode_nr ino,char * name,__u16 i_mode)
{/*已成功申请到inode，然后在父目录中添加目录项，成功返回0，失败返回-1*/
	int blkcnt;
	int i,n;
	__u32 *p = NULL;
	__u64 blkaddr;
	unsigned int info[4] = {0};
	unsigned int tmp;
	struct neo_inode parent;
	struct neo_dir_entry dirent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);	/*读入父目录inode*/

	
	blkcnt = parent.i_blocks;
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	//printf("blkcnt : %d",blkcnt);
	/*首先，必须把所有目录项都检索一遍看是有重名*/
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0){
			printf("same name\n");
			errno = EEXIST;
			return -1;
		}
	}
	if (blkcnt > 12){/*dir file's max blocks count is 13,block[12] for indirect addr.*/
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);		/*4 = sizeof(__32)*/
		fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0){
				printf("same name\n");
				errno = EEXIST;
				return -1;
			}
		}
	}
	/*然后，查找空位来加入目录项*/

	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	memset(dirent.name,0,MAX_FILE_NAME);
	dirent.inode = ino;
	dirent.name_len = strlen(name);
	strcpy(dirent.name,name);
	dirent.file_type = (__u8)i_mode;
	//dirent.rec_len = (4 - dirent.name_len%4) + dirent.name_len + 8;
	
	if (parent.i_blocks == 0){
		parent.i_block[0] = get_block(parent_ino);
		parent.i_blocks += 1;
		dirent.rec_len = BLOCK_SIZE;				/*第一项同时也是最后一项*/
		fseek(fp,block_to_addr(parent.i_block[0]),SEEK_SET);
		//fwrite(&dirent,((dirent.name_len%4?(4 - dirent.name_len%4 + dirent.name_len):(dirent.name_len)) + 8),1,fp);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

		return 0;
	}

	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_empty_dentry(blkaddr,name,info) == 0){	/*找到空闲位置后根据info维护数据结构*/
			write_dentry(blkaddr,info,dirent);

			fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
			fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

			return 0;
		}
	
	}
	if (blkcnt > 12){/*dir file's max blocks count is 13,block[12] for indirect addr.*/
		n = blkcnt - 12;
		fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_empty_dentry(blkaddr,name,info) == 0){
				write_dentry(blkaddr,info,dirent);

				fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
				fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

				return 0;
			}
		}
	}
	/*如果仍未分配到，即现有block都满了，再申请一块*/
	if (blkcnt < 12){
		parent.i_block[blkcnt] = get_block(parent_ino);
		parent.i_blocks += 1;
		dirent.rec_len = BLOCK_SIZE;		/*第一项同时也是最有一项*/
		fseek(fp,block_to_addr(parent.i_block[blkcnt]),SEEK_SET);
		//fwrite(&dirent,((dirent.name_len%4?(4 - dirent.name_len%4 + dirent.name_len):(dirent.name_len)) + 8),1,fp);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

		return 0;
	}else if (blkcnt = 12){
		parent.i_block[12] = get_block(parent_ino);
		tmp = get_block(parent_ino);
		parent.i_blocks += 1;
		fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
		fwrite(&tmp,4,1,fp);
		dirent.rec_len = BLOCK_SIZE;		/*第一项同时也是最有一项*/
		fseek(fp,block_to_addr(tmp),SEEK_SET);
		//fwrite(&dirent,((dirent.name_len%4?(4 - dirent.name_len%4 + dirent.name_len):(dirent.name_len)) + 8),1,fp);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

		return 0;
	}else if (blkcnt < 1036){			/*1036 = 4096/4 + 12，即目录文件的最大block数量*/
		tmp = get_block(parent_ino);
		parent.i_blocks += 1;
		fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
		fseek(fp,(blkcnt - 12) * 4,SEEK_CUR);
		fwrite(&tmp,4,1,fp);
		dirent.rec_len = BLOCK_SIZE;		/*第一项同时也是最有一项*/
		fseek(fp,block_to_addr(tmp),SEEK_SET);
		//fwrite(&dirent,((dirent.name_len%4?(4 - dirent.name_len%4 + dirent.name_len):(dirent.name_len)) + 8),1,fp);
		fwrite(&dirent,TRUE_LEN(dirent.name_len),1,fp);

		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);	/*写回父目录inode*/

		return 0;
	}else{
		errno = ENOSPC;
		return -1;
	}

	free(p);
	return 0;

}

int delete_dentry(inode_nr parent_ino,char * name,__u16 i_mode)
{
	/*删除目录项*/
	int blkcnt;
	int i,n;
	__u32 *p = NULL;
	__u64 blkaddr;
	unsigned int info[4] = {0};
	unsigned int tmp;
	struct neo_inode parent;
	struct neo_dir_entry dirent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);	/*读入父目录inode*/

	blkcnt = parent.i_blocks;
	if (blkcnt <= 12)
		n = blkcnt;
	else
		n = 12;
	for (i = 0; i < n; i++){
		blkaddr = block_to_addr(parent.i_block[i]);
		if (blk_search_dentry(blkaddr,name,info) == 0){
			delete_block_dentry(parent_ino,i,blkaddr,info);
			return 0;
		}
	}
	if (blkcnt > 12){/*dir file's max blocks count is 13,block[12] for indirect addr.*/
		n = blkcnt - 12;
		p = (__u32 *)malloc(4 * n);		/*4 = sizeof(__32)*/
		fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
		fread(p,(4 * n),1,fp);
		for (i = 0; i < n; i++){
			blkaddr = block_to_addr(p[i]);
			if (blk_search_dentry(blkaddr,name,info) == 0){
				delete_block_dentry(parent_ino,(i + 12),blkaddr,info);
				free(p);
				return 0;
			}
		}
	}
	return 0;
}

void delete_block_dentry(inode_nr parent_ino,int blknr,__u64 blkaddr,unsigned int info[])
{
	block_nr tmpnr;
	unsigned short true_cur_len,true_prev_len;
	struct neo_dir_entry prev;
	struct neo_dir_entry del;
	struct neo_dir_entry blank;
	struct neo_inode parent;

	fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
	fread(&parent,sizeof(struct neo_inode),1,fp);
	fseek(fp,(blkaddr + info[2]),SEEK_SET);
	fread(&prev,8,1,fp);
	fseek(fp,(blkaddr + info[3]),SEEK_SET);
	fread(&del,8,1,fp);
	true_prev_len = TRUE_LEN(prev.name_len);
	true_cur_len = TRUE_LEN(del.name_len);

 /*
	printf("info[0] is %d\n",info[0]);
	printf("info[1] is %d\n",info[1]);
	printf("info[2] is %d\n",info[2]);
	printf("info[3] is %d\n",info[3]);
// */

	if ((info[2] == info[3]) && ((info[1] + info[3]) == 4096)){			/*1.此目录项是此块中的最后一项*/
		if (blknr == (parent.i_blocks - 1)){	/*释放的块为最后一块*/
			if (blknr <= 11){
				free_block(parent.i_block[blknr]);
				parent.i_block[blknr] = 0;
			}else if (blknr == 12){
				fseek(fp,block_to_addr(parent.i_block[12]),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
				free_block(parent.i_block[12]);
				parent.i_block[12] = 0;
			}else {
				fseek(fp,(block_to_addr(parent.i_block[12]) + (blknr - 12) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
			}
		}else {					/*释放的块不是最后一块*/
			if (blknr <= 11){
				free_block(parent.i_block[blknr]);
				fseek(fp,(block_to_addr(parent.i_block[12]) + (parent.i_blocks - 1 - 12) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				parent.i_block[blknr] = tmpnr;
			}else {
				fseek(fp,(block_to_addr(parent.i_block[12]) + (blknr - 12) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				free_block(tmpnr);
				fseek(fp,(block_to_addr(parent.i_block[12]) + (parent.i_blocks - 1 - 12) * 4),SEEK_SET);
				fread(&tmpnr,4,1,fp);
				fseek(fp,(block_to_addr(parent.i_block[12]) + (blknr - 12) * 4),SEEK_SET);
				fwrite(&tmpnr,4,1,fp);
			}
		}
		parent.i_blocks --;
		fseek(fp,inode_to_addr(parent_ino),SEEK_SET);
		fwrite(&parent,sizeof(struct neo_inode),1,fp);
	}else if ((info[2] == info[3]) && (info[2] == 0)){	/*此目录项是第一个项,顶头*/
		del.inode = 0;
		fseek(fp,blkaddr,SEEK_SET);
		fwrite(&del,8,1,fp);
	}else if ((info[2] == info[3]) && (info[2] != 0)){	/*此目录项是第一个项,前是空白*/
		fseek(fp,blkaddr,SEEK_SET);
		fread(&blank,8,1,fp);
		blank.rec_len += del.rec_len;
		fseek(fp,blkaddr,SEEK_SET);
		fwrite(&blank,8,1,fp);
	}else if (prev.rec_len > true_prev_len){		/*此目录项不在开头,前是空白*/
		prev.rec_len += del.rec_len;
		fseek(fp,(blkaddr + info[2]),SEEK_SET);
		fwrite(&prev,8,1,fp);
		fseek(fp,(blkaddr + info[2] + true_prev_len),SEEK_SET);
		fread(&blank,8,1,fp);
		blank.rec_len += del.rec_len;
		fseek(fp,(blkaddr + info[2] + true_prev_len),SEEK_SET);
		fwrite(&blank,8,1,fp);
	}else if (prev.rec_len == true_prev_len){		/*此目录项不在开头,前非空白*/
		prev.rec_len += del.rec_len;
		fseek(fp,(blkaddr + info[2]),SEEK_SET);
		fwrite(&prev,8,1,fp);
		del.inode = 0;
		fseek(fp,(blkaddr + info[3]),SEEK_SET);
		fwrite(&del,8,1,fp);
	}
}

int blk_search_dentry(__u64 blkaddr,char *name,unsigned int info[])
{//在存放目录项的block中查找文件名为name的目录项，成功返回0，失败返回-1。

	unsigned int offset_prev = 0;		/*记录块内上一个目录项的偏移；*/
	unsigned int offset_cur = 0;		/*记录块内当前目录项的偏移；*/
	struct neo_dir_entry *cur;		/*临时存放读取的目录项*/
	char cname[MAX_FILE_NAME] = {'\0'};
	void *block;				/*此处未考虑移植扩展性，void *只在gcc中可以运算，ansi C并不支持*/
						/*故指针移动通过计算block实现，然后cur跟进*/

	block = (void *)malloc(BLOCK_SIZE);
	cur = block;
	//printf("blkaddr : %ld",blkaddr);
	fseek(fp,blkaddr,SEEK_SET);
	fread(block,BLOCK_SIZE,1,fp);		/*将此块读入内存*/

	if(cur->inode == 0){			/*第一个是空块，此时将block指向第一个目录项*/
		block += cur->rec_len;
		offset_prev += cur->rec_len;
		offset_cur += cur->rec_len;
	}
	do {/*当cur还有下一项*/
		cur = block;
		strncpy(cname,cur->name,cur->name_len);
		if (strcmp(cname,name) == 0){
			info[0] = cur->inode;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			return 0;
		}
 /*
		printf("prev: %d   ",offset_prev);
		printf("cur: %d\n\n",offset_cur);
		printf("cur->rec_len: %d   ",cur->rec_len);
		printf("cur->name_len: %d   ",cur->name_len);
		printf("cur->name: %s\n\n",cur->name);
// */
		offset_prev = offset_cur;
		offset_cur += cur->rec_len;
		block += cur->rec_len;
		memset(cname,0,MAX_FILE_NAME);
	}
	while ((offset_prev + cur->rec_len) != 4096 );
	return -1;
}

int blk_search_empty_dentry(__u64 blkaddr,char *name,unsigned int info[])
{/*在存放目录项的block中查找文件名为name的目录项，成功返回0，空间已满返回-1，*/

	unsigned int offset_prev = 0;		/*记录块内上一个目录项的偏移；*/
	unsigned int offset_cur = 0;		/*记录块内当前目录项的偏移；*/
	unsigned int order = 0;		
	struct neo_dir_entry *cur;		/*临时存放读取的目录项*/
	struct neo_dir_entry *blank;		/*临时存放目录项下一个空闲块*/
	unsigned char need_len;
	unsigned short true_len;
	void *block;				/*此处未考虑移植扩展性，void *只在gcc中可以运算，ansi C并不支持*/
						/*故指针移动通过计算block实现，然后cur跟进*/
	//need_len = ((strlen(name)%4)?((4 - strlen(name)%4) + strlen(name)):(strlen(name))) + 16;
	need_len = TRUE_LEN(strlen(name)) + 8;

	block = (void *)malloc(BLOCK_SIZE);
	cur = block;
	//printf("blkaddr : %ld",blkaddr);
	fseek(fp,blkaddr,SEEK_SET);
	fread(block,BLOCK_SIZE,1,fp);		/*将此块读入内存*/

	if(cur->inode == 0){			/*第一个是空块，此时将block指向第一个目录项*/
		if (cur->rec_len >= need_len){
			info[0] = order;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			return 0;
		}else {
			offset_prev += cur->rec_len;
			offset_cur += cur->rec_len;
			block += cur->rec_len;
		}
	}
	do {/*当cur还有下一项*/
		order ++;
		cur = block;
		//true_len = ((cur->name_len%4)?((4 - cur->name_len%4) + cur->name_len):(cur->name_len)) + 8;
		true_len = TRUE_LEN(cur->name_len);
		if ((cur->rec_len - true_len) > need_len){
			info[0] = order;
			info[1] = cur->rec_len;
			info[2] = offset_prev;
			info[3] = offset_cur;
			return 0;
		}
 /*
		printf("prev: %d   ",offset_prev);
		printf("cur: %d\n\n",offset_cur);
		printf("cur->rec_len: %d   ",cur->rec_len);
		printf("cur->name_len: %d   ",cur->name_len);
// */
		offset_prev = offset_cur;
		offset_cur += cur->rec_len;
		block += cur->rec_len;
	}
	while ((offset_prev + cur->rec_len) != 4096 );
	return -1;
}

void write_dentry(__u64 blkaddr,unsigned int info[],struct neo_dir_entry dirent)
{
	unsigned short true_len;
	unsigned short mid;
	struct neo_dir_entry blank;
	struct neo_dir_entry tmp;
	blank.inode = 0;
	blank.name_len = 0;
	blank.file_type = 0;

 /*
	printf("info[0] is %d\n",info[0]);
	printf("info[1] is %d\n",info[1]);
	printf("info[2] is %d\n",info[2]);
	printf("info[3] is %d\n",info[3]);
// */
	//true_len = (dirent.name_len%4?(4 - dirent.name_len%4 + dirent.name_len):(dirent.name_len)) + 8;
	true_len = TRUE_LEN(dirent.name_len);
	if (info[0] == 0){				/*空闲区域在此块的开头*/
		dirent.rec_len = info[1];
		fseek(fp,blkaddr,SEEK_SET);
		fwrite(&dirent,true_len,1,fp);
		
		blank.rec_len = info[1] - true_len;
		fwrite(&blank,8,1,fp);
	}else if ((info[1] + info[3]) == 4096){		/*空闲区域在此块的末尾*/
		fseek(fp,blkaddr + info[3],SEEK_SET);
		fread(&tmp,8,1,fp);
		//tmp.rec_len = (tmp.name_len%4?(4 - tmp.name_len%4 + tmp.name_len):(tmp.name_len)) + 8;
		tmp.rec_len = TRUE_LEN(tmp.name_len);
		fseek(fp,-8,SEEK_CUR);
		fwrite(&tmp,8,1,fp);

		dirent.rec_len = 4096 - info[3] - tmp.rec_len;
		fseek(fp,blkaddr + info[3] + tmp.rec_len,SEEK_SET);
		fwrite(&dirent,true_len,1,fp);

		blank.rec_len = dirent.rec_len - true_len;
		fwrite(&blank,8,1,fp);
	}else {						/*空闲区域在此块的中间*/
		fseek(fp,blkaddr + info[3],SEEK_SET);
		fread(&tmp,8,1,fp);
		mid = tmp.rec_len;
		//tmp.rec_len = (tmp.name_len%4?(4 - tmp.name_len%4 + tmp.name_len):(tmp.name_len)) + 8;
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

inode_nr get_inode(inode_nr ino,__u16 i_mode)
{/*从父目录ino下分配一个inode，同时写好sb和gdt，写好位图*/
	int i,j;
	unsigned char c;
	unsigned int aver_free_inodes,aver_free_blocks;
	inode_nr res;
	int groupcnt = neo_sb_info.s_groups_count;
	int bgnr = ino / 8192;				/*8192即每组inode个数*/
	int prev,tag = 0;
	if (i_mode == 1){				/*给普通文件申请inode*/
		for (i = 0; i < groupcnt; i++){
			if (neo_gdt[bgnr].bg_free_inodes_count > 0){
				neo_sb_info.s_free_inodes_count --;
				neo_gdt[bgnr].bg_free_inodes_count --;
				write_sb_gdt_main(bgnr);
				break;
			}
			bgnr = (bgnr + 1)%groupcnt;
		}
	}else {						/*给目录文件分配inode*/
		aver_free_inodes = neo_sb_info.s_free_inodes_count / neo_sb_info.s_groups_count;
		aver_free_blocks = neo_sb_info.s_free_blocks_count / neo_sb_info.s_groups_count;
		for (i = 0; i < groupcnt; i++){
			prev = (bgnr - 1 + groupcnt)%groupcnt;
			if ((neo_gdt[bgnr].bg_free_inodes_count > 0) && \
				(neo_gdt[bgnr].bg_used_dirs_count < neo_gdt[prev].bg_used_dirs_count) && \
				(neo_gdt[bgnr].bg_free_inodes_count > aver_free_inodes) && \
				(neo_gdt[bgnr].bg_free_blocks_count > aver_free_blocks)){
				neo_sb_info.s_free_inodes_count --;
				neo_gdt[bgnr].bg_free_inodes_count --;
				neo_gdt[bgnr].bg_used_dirs_count ++;
				write_sb_gdt_main(bgnr);
				tag = 1;
				break;
			}
			bgnr = (bgnr + 1)%groupcnt;
		}/*未找到最优块组，则线性从当前组查找*/
		if (tag == 0){
			for (i = 0; i < groupcnt; i++){
				if (neo_gdt[bgnr].bg_free_inodes_count > 0){
					neo_sb_info.s_free_inodes_count --;
					neo_gdt[bgnr].bg_free_inodes_count --;
					neo_gdt[bgnr].bg_used_dirs_count ++;
					write_sb_gdt_main(bgnr);
					break;
				}
				bgnr = (bgnr + 1)%groupcnt;
			}
		}
	}
	if (ibcache.groupnr != bgnr){
		if (ibcache.groupnr != -1){
			fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
			fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_inode_bitmap),SEEK_SET);
		//printf("addr = %d\n",block_to_addr(neo_gdt[bgnr].bg_block_bitmap));
		fread(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		ibcache.groupnr = bgnr;
	}
	for (i = 0; i < BLOCK_SIZE; i++)		/*32 is the first 256 + 2or4 used blocks in the bitmap*/
	{
		//printf("bbitmap[%d] = %x\n",i,bbcache.bbitmap[i]);
		if (ibcache.ibitmap[i] != 0xFF){	/*find empty block*/
			for (j = 0, c = 0x80; j < 8; j++){
				//printf("c = %x\n",c);
				if ((ibcache.ibitmap[i]&c) == 0){
					ibcache.ibitmap[i] += c;
					//printf("i = %d\n",i);
					//printf("j = %d\n",j);
					//printf("c = %x\n",c);
					res = neo_sb_info.s_inodes_per_group * bgnr + 8 * i + j;
					init_inode(res,i_mode);
					return res;
				}
				c = c >> 1;
			}
		}
	}
}

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

block_nr get_block(inode_nr ino)
{/*inode只是申请策略所需，尽量申请inode所在组的块*/
	int i,j;
	unsigned char c;
	int groupcnt = neo_sb_info.s_groups_count;
	bg_nr bgnr = ino / 8192;			/*8192即每组inode个数*/
	for (i = 0; i < groupcnt; i++){
		if (neo_gdt[bgnr].bg_free_blocks_count > 0){
			neo_sb_info.s_free_blocks_count --;
			neo_gdt[bgnr].bg_free_blocks_count --;
			write_sb_gdt_main(bgnr);
			break;
		}
		bgnr = (bgnr + 1)%groupcnt;
	}
	if (bbcache.groupnr != bgnr){
		if (bbcache.groupnr != -1){
			fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
			fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_block_bitmap),SEEK_SET);
		//printf("addr = %d\n",block_to_addr(neo_gdt[bgnr].bg_block_bitmap));
		fread(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		bbcache.groupnr = bgnr;
	}
	for (i = FIRST_FREE_BLOCK; i < BLOCK_SIZE; i++)		/*32 is the first 256 + 2or4 used blocks in the bitmap*/
	{
		//printf("bbitmap[%d] = %x\n",i,bbcache.bbitmap[i]);
		if (bbcache.bbitmap[i] != 0xFF){	/*find empty block*/
			for (j = 0, c = 0x80; j < 8; j++){
				//printf("c = %x\n",c);
				if ((bbcache.bbitmap[i]&c) == 0){
					bbcache.bbitmap[i] += c;
					//printf("i = %d\n",i);
					//printf("j = %d\n",j);
					//printf("c = %x\n",c);
					return (BLOCKS_PER_GROUP * bgnr + 8 * i + j);
				}
				c = c >> 1;
			}
		}
	}
}

int free_inode(inode_nr ino)
{
	bg_nr bgnr = ino / 8192;
	__u16 i_mode;
	int l = (ino % 8192) / 8;
	int r = (ino % 8192) % 8;
	__u64 addr;
	unsigned char c = 0x80;
	struct neo_inode del_inode;
	addr = inode_to_addr(ino);
	fseek(fp,addr,SEEK_SET);
	fread(&del_inode,1,sizeof(struct neo_inode),fp);
	i_mode = del_inode.i_mode;
	if ((i_mode == 2) && (del_inode.i_blocks != 0)){
		errno = ENOTEMPTY;
		return -1;
	}
	if (ibcache.groupnr != bgnr){
		if (ibcache.groupnr != -1){
			fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
			fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_inode_bitmap),SEEK_SET);
		//printf("addr = %d\n",block_to_addr(neo_gdt[bgnr].bg_block_bitmap));
		fread(ibcache.ibitmap,1,BLOCK_SIZE,fp);
		ibcache.groupnr = bgnr;
	}
	neo_sb_info.s_free_inodes_count ++;
	neo_gdt[bgnr].bg_free_inodes_count ++;
	if (i_mode == 2)
		neo_gdt[bgnr].bg_used_dirs_count --;
	write_sb_gdt_main(bgnr);
	ibcache.ibitmap[l] -= (c >> r);
	if ((i_mode == 1) && (del_inode.i_blocks != 0)){
		free_selected_blocks(del_inode.i_block,0,(del_inode.i_blocks - 1));
	}
	return 0;
}

void get_selected_blocks(__u32 *i_block,inode_nr ino,__u32 start,__u32 end)
{
	int i;
	int blk_n,blk_r;
	block_nr blknr,iblknr;
	__u64 iaddr;
	if (end <= 11){
		for (i = start; i <= end; i ++)
			i_block[i] = get_block(ino);
	}else if (end <= 1035){
		if (start <= 12)
			i_block[12] = get_block(ino);

		for (i = start; i <= 11; i ++)
			i_block[i] = get_block(ino);
		iaddr = block_to_addr(i_block[12]);
		for (i = ((start > 12) ? start : 12); i <= end; i ++){
			blknr = get_block(ino);
			fseek(fp,iaddr + (i - 12) * 4,SEEK_SET);
			fwrite(&blknr,4,1,fp);
		}
	}else {
		if (start <= 1036)
			i_block[13] = get_block(ino);

		for (i = start; i <= 1035; i ++){
			blknr = get_block(ino);
			fseek(fp,(block_to_addr(i_block[12]) + (i - 12) * 4),SEEK_SET);
			fwrite(&blknr,4,1,fp);
		}
		for (i = ((start > 1036) ? start : 1036); i <= end; i ++){
			blk_n = (i - 1036) / 1024;
			blk_r = (i - 1036) % 1024;
			if (blk_r == 0){
				iblknr = get_block(ino);
				fseek(fp,(block_to_addr(i_block[13]) + blk_n * 4),SEEK_SET);
				fwrite(&iblknr,4,1,fp);
				blknr = get_block(ino);
				fseek(fp,block_to_addr(iblknr),SEEK_SET);
				fwrite(&blknr,4,1,fp);
			}else {
				fseek(fp,(block_to_addr(i_block[13]) + blk_n * 4),SEEK_SET);
				fread(&iblknr,4,1,fp);
				blknr = get_block(ino);
				fseek(fp,(block_to_addr(iblknr) + blk_r * 4),SEEK_SET);
				fwrite(&blknr,4,1,fp);
			}
		}
	}
}

void free_selected_blocks(__u32 *i_block,__u32 start,__u32 end)
{/*直接索引，1级间接，二级间接对应块区间分别是：0~11,12~1035,1036~1049611*/
	int i,j;
	int begin_page,end_page,begin_remainder,end_remainder;
	void *block = NULL;
	void *sub_block = NULL;
	__u32 *p = NULL;
	__u32 *q = NULL;
	if (end <= 11){
		for (i = start; i <= end; i ++){
			free_block(i_block[i]);
		}
	}else if (end <= 1035){
		for (i = start; i <= 11; i ++)
			free_block(i_block[i]);
		block = (void *)malloc(BLOCK_SIZE);
		p = block;
		fseek(fp,block_to_addr(i_block[12]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		for (i = ((start > 11) ? start : 12) - 12; i <= end -12; i ++)
			free_block(*(p + i));
		if (start <= 12)
			free_block(i_block[12]);
	}else {
		for (i = start; i <= 11; i ++)
			free_block(i_block[i]);

		block = (void *)malloc(BLOCK_SIZE);
		p = block;
		sub_block = (void *)malloc(BLOCK_SIZE);
		q = sub_block;
		fseek(fp,block_to_addr(i_block[12]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		for (i = ((start > 11) ? start : 12) - 12; i <= 1023; i ++)
			free_block(*(p + i));

		fseek(fp,block_to_addr(i_block[13]),SEEK_SET);
		fread(block,BLOCK_SIZE,1,fp);
		begin_page = (((start > 1035) ? start : 1036) - 1036) / 1024;
		begin_remainder = (((start > 1035) ? start : 1036) - 1036) % 1024;
		end_page = (end - 1036) / 1024;
		end_remainder = (end - 1036) % 1024;
		if (begin_page == end_page){
			fseek(fp,block_to_addr(*(p + begin_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = begin_remainder; i <= end_remainder; i ++){
				free_block(*(q + i));
			}
			if (begin_remainder == 0)
				free_block(*(p + begin_page));
		}else {
			fseek(fp,block_to_addr(*(p + begin_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = begin_remainder; i <= 1023; i ++){
				free_block(*(q + i));
			}
			if (begin_remainder == 0)
				free_block(*(p + begin_page));

			for (i = begin_page + 1; i <= end_page - 1; i ++){
				fseek(fp,block_to_addr(*(p + i)),SEEK_SET);
				fread(sub_block,BLOCK_SIZE,1,fp);
				for (j = 0; j <= 1023; j ++)
					free_block(*(q + j));
				free_block(*(p + i));
			}

			fseek(fp,block_to_addr(*(p + end_page)),SEEK_SET);
			fread(sub_block,BLOCK_SIZE,1,fp);
			for (i = 0; i <= end_remainder; i ++){
				free_block(*(q + i));
			}
			free_block(*(p + end_page));
		}
		if (start <= 12){
			free_block(i_block[12]);
			free_block(i_block[13]);
		}else if (start <= 1036)
			free_block(i_block[13]);
	}
}

void free_block(block_nr blk)
{
	bg_nr bgnr = blk / BLOCKS_PER_GROUP;
	int l = (blk % BLOCKS_PER_GROUP) / 8;
	int r = (blk % BLOCKS_PER_GROUP) % 8;
	unsigned char c = 0x80;
	if (bbcache.groupnr != bgnr){
		if (bbcache.groupnr != -1){
			fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
			fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		}
		fseek(fp,block_to_addr(neo_gdt[bgnr].bg_block_bitmap),SEEK_SET);
		//printf("addr = %d\n",block_to_addr(neo_gdt[bgnr].bg_block_bitmap));
		fread(bbcache.bbitmap,1,BLOCK_SIZE,fp);
		bbcache.groupnr = bgnr;
	}
	neo_sb_info.s_free_blocks_count ++;
	neo_gdt[bgnr].bg_free_blocks_count ++;
	write_sb_gdt_main(bgnr);
	bbcache.bbitmap[l] -= (c >> r);
}

void write_sb_gdt_main(bg_nr bgnr)
{
	fseek(fp,1024,SEEK_SET);
	//print_sb(neo_sb_info);
	fwrite(&neo_sb_info,sizeof(struct neo_super_block),1,fp);
	fseek(fp,4096 + (bgnr * sizeof(struct neo_group_desc)),SEEK_SET);
	//print_gdt(neo_gdt,neo_sb_info.s_groups_count);
	fwrite((neo_gdt + bgnr),sizeof(struct neo_group_desc),1,fp);
}

void write_sb_gdt_backups()
{
}

void write_bitmap()
{
	if (bbcache.groupnr != -1){
		fseek(fp,block_to_addr(neo_gdt[bbcache.groupnr].bg_block_bitmap),SEEK_SET);
		fwrite(bbcache.bbitmap,1,BLOCK_SIZE,fp);
	}
	if (ibcache.groupnr != -1){
		fseek(fp,block_to_addr(neo_gdt[ibcache.groupnr].bg_inode_bitmap),SEEK_SET);
		fwrite(ibcache.ibitmap,1,BLOCK_SIZE,fp);
	}
}

void print_sb(struct neo_super_block neo_sb_info)
{
	printf("super block:\n");
	printf("sb inodes count: %d\n",neo_sb_info.s_inodes_count);
	printf("sb blocks count: %d\n",neo_sb_info.s_blocks_count);
	printf("sb groups count: %d\n",neo_sb_info.s_groups_count);
	printf("sb free inodes count: %d\n",neo_sb_info.s_free_inodes_count);
	printf("sb free blocks count: %d\n",neo_sb_info.s_free_blocks_count);
	printf("sb log block size: %d\n",neo_sb_info.s_log_block_size);
	printf("sb blocks/group: %d\n",neo_sb_info.s_blocks_per_group);
	printf("sb inodes/group: %d\n",neo_sb_info.s_inodes_per_group);
	printf("sb magic#: %d\n",neo_sb_info.s_magic);
	printf("sb inode size: %d\n\n",neo_sb_info.s_inode_size);
}

void print_gdt(struct neo_group_desc *gdt,int groupcnt)
{
	int i;
	printf("GDT:\n");
	for (i = 0; i < groupcnt; i++){
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
	int groupnr,r;
	int offset = BLOCK_SIZE * 2;			//block和inode的位图
	groupnr = ino / neo_sb_info.s_inodes_per_group;
	r = ino % neo_sb_info.s_inodes_per_group;
	if(is_powerof_357(groupnr))
		offset += BLOCK_SIZE * 2;		//SB&GDT 2Blocks
	offset += (BLOCK_SIZE * BLOCKS_PER_GROUP * groupnr + r * neo_sb_info.s_inode_size);
	//printf("inode %d addr is %d\n\n",ino,offset);
	return offset;
}

__u64 i_block_to_addr(block_nr blknr,block_nr i_block[])
{
	__u64 blkaddr;
	__u32 iiblk_n,iiblk_r,blk,iblk;
	if (blknr <= 11){
		blkaddr = block_to_addr(i_block[blknr]);
	}else if (blknr <= 1035){
		fseek(fp,(block_to_addr(i_block[12]) + ((blknr - 12) * 4)),SEEK_SET);
		fread(&blk,4,1,fp);
		blkaddr = block_to_addr(blk);
	}else {
		iiblk_n = (blknr - 1036) / 1024;
		iiblk_r = (blknr - 1036) % 1024;
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
	return (blk * 4096);
}

int is_powerof_357(int i)
{
	if (pow(3,(int)(float)(log(i)/log(3))) == i || pow(5,(int)(float)(log(i)/log(5))) == i || 
		pow(7,(int)(float)(log(i)/log(7))) == i || i == 0)
		return 1;
	return 0;
}









