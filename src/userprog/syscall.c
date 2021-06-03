#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* added for HW3 */
#include "threads/vaddr.h"
#include "filesys/off_t.h"

static void syscall_handler (struct intr_frame *);
/* added for HW3 */
void check_vaddr(const void *vaddr);
struct lock sys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&sys_lock);
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	/* adjusted for HW3 */
	switch (*(uint32_t *)(f->esp)) {
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			check_vaddr(f->esp + 4);
			exit((int)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_EXEC:
			check_vaddr(f->esp + 4);
			f->eax = exec((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_WAIT:
			check_vaddr(f->esp + 4);
			f->eax = wait((pid_t)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_CREATE:
			check_vaddr(f->esp + 4);
			check_vaddr(f->esp + 8);
			f->eax = create((const char *)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
			break;
		case SYS_REMOVE:
			check_vaddr(f->esp + 4);
			f->eax = remove((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_OPEN:
			check_vaddr(f->esp + 4);
			f->eax = open((const char *)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_FILESIZE:
			check_vaddr(f->esp + 4);
			f->eax = filesize((int)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_READ:
			check_vaddr(f->esp + 4);
			check_vaddr(f->esp + 8);
			check_vaddr(f->esp + 12);
			f->eax = read((int)*(uint32_t *)(f->esp + 4), (void *)*(uint32_t *)(f->esp + 8), (unsigned)*((uint32_t *)(f->esp + 12)));
			break;
		case SYS_WRITE:
			check_vaddr(f->esp + 4);
			check_vaddr(f->esp + 8);
			check_vaddr(f->esp + 12);
			f->eax = write((int)*(uint32_t *)(f->esp + 4), (void *)*(uint32_t *)(f->esp + 8), (unsigned)*((uint32_t *)(f->esp + 12)));
			break;
		case SYS_SEEK:
			check_vaddr(f->esp + 4);
			check_vaddr(f->esp + 8);
			seek((int)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
			break;
		case SYS_TELL:
			check_vaddr(f->esp + 4);
			f->eax = tell((int)*(uint32_t *)(f->esp + 4));
			break;
		case SYS_CLOSE:
			check_vaddr(f->esp + 4);
			close((int)*(int32_t *)(f->esp + 4));
			break;
	}
  // printf ("system call!\n");
  // thread_exit ();
}

/* added for HW3 */
struct file
{
	struct inode *inode;
	off_t pos;
	bool deny_write;
};

void
check_vaddr (const void *vaddr)
{
	if (!is_user_vaddr(vaddr)){
		exit(-1);
	}
}

void
halt (void)
{
	shutdown_power_off();
}

void
exit (int status)
{
	printf("%s: exit(%d)\n", thread_current()->name, status);
	thread_current()->exit_status = status;
	int i;
	for (i = 3; i < 128; i++){
		if (thread_current()->fd[i] != NULL){
			close(i);
		}
	}
	struct thread *t = NULL;
	struct list_elem *e = NULL;
	for (e = list_begin(&thread_current()->child); e != list_end(&thread_current()->child); e = list_next(e)){
		t = list_entry(e, struct thread, child_elem);
		process_wait(t->tid);
	}
	thread_exit();
}

pid_t
exec (const char *cmd_line)
{
	return (pid_t)(process_execute(cmd_line));
}

int
wait (pid_t pid)
{
	return process_wait((tid_t)pid);
}

bool
create (const char *file, unsigned initial_size)
{
	if (file == NULL){
		exit(-1);
	}
	return filesys_create(file, initial_size);
}

bool
remove (const char *file)
{
	if (file == NULL){
		exit(-1);
	}
	return filesys_remove(file);
}

int
open (const char *file)
{
	int ans = -1;
	if (file == NULL){
		exit(-1);
	}
	lock_acquire(&sys_lock);
	struct file *f = filesys_open(file);
	if (f == NULL){
		ans = -1;
	}
	else{
		if (strcmp(thread_current()->name, file) == 0){
			file_deny_write(f);
		}
		int i;
		for (i = 3; i < 128; i++){
			if (thread_current()->fd[i] == NULL){
				thread_current()->fd[i] = f;
				ans = i;
				break;
			}
		}
	}
	lock_release(&sys_lock);
	return ans;
}

int
filesize (int fd)
{
	if (thread_current()->fd[fd] == NULL){
		exit(-1);
	}
	return file_length(thread_current()->fd[fd]);
}

int
read (int fd, void *buffer, unsigned size)
{
	int ans = -1;
	check_vaddr(buffer);
	lock_acquire(&sys_lock);
	int i;
	if (fd == 0){
		for (i = 0; i < size; i++){
			if (((char *)buffer)[i] == '\0'){
				break;
			}
		}
		ans = i;
	}
	else if (fd > 2){
		if (thread_current()->fd[fd] == NULL){
			lock_release(&sys_lock);
			exit(-1);
		}
		ans = file_read(thread_current()->fd[fd], buffer, size);
	}
	lock_release(&sys_lock);
	return ans;
}

int
write (int fd, const void *buffer, unsigned size)
{
	int ans = -1;
	lock_acquire(&sys_lock);
	if (fd == 1){
		putbuf(buffer, size);
		ans = size;
	}
	else if (fd > 2){
		if (thread_current()->fd[fd] == NULL){
			lock_release(&sys_lock);
			exit(-1);
		}
		if (thread_current()->fd[fd]->deny_write){
			file_deny_write(thread_current()->fd[fd]);
		}
		ans = file_write(thread_current()->fd[fd], buffer, size);
	}
	lock_release(&sys_lock);
	return ans;
}

void
seek (int fd, unsigned position)
{
	if (thread_current()->fd[fd] == NULL){
		exit(-1);
	}
	file_seek(thread_current()->fd[fd], position);
}

unsigned
tell (int fd)
{
	if (thread_current()->fd[fd] == NULL){
		exit(-1);
	}
	return (unsigned)(file_tell(thread_current()->fd[fd]));
}

void
close (int fd)
{
	if (thread_current()->fd[fd] == NULL){
		exit(-1);
	}
	struct file *f = thread_current()->fd[fd];
	thread_current()->fd[fd] = NULL;
	file_close(f);
}

