// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	pde_t pde = uvpd[PDX(addr)];
	pte_t pte = uvpt[utf->utf_fault_va / PGSIZE];
	if (!((err & FEC_WR) && (pde & PTE_COW) && (pte & PTE_COW))) {
		panic("invalid pgfault, addr=%p, err=%p, pde=%p, pte=%p\n", addr, err, pde, pte);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	void *pg_addr = (void*)ROUNDDOWN((uintptr_t)addr, PGSIZE);
	envid_t curenv = sys_getenvid();
	r = sys_page_alloc(curenv, PFTEMP, PTE_P | PTE_U | PTE_W);
	if (r) { panic("sys_page_alloc:%e\n", r); }
	memmove(PFTEMP, pg_addr, PGSIZE);
	r = sys_page_map(curenv, PFTEMP, curenv, pg_addr, PTE_P | PTE_U | PTE_W);
	if (r) { panic("sys_page_map:%e\n", r); }
	r = sys_page_unmap(curenv, PFTEMP);
	if (r) { panic("sys_page_unmap:%e\n", r); }
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	envid_t father = sys_getenvid();
	void *va = (void*)(pn * PGSIZE);
	// check page permission of current process
	pde_t pde = uvpd[PDX(va)];
	pte_t pte = uvpt[pn];

    if (pte & PTE_SHARE) {
        return sys_page_map(father, va, envid, va, pte & PTE_SYSCALL);
    }

    int perm = 0;
    bool cow = false;
    if ((pde & (PTE_W | PTE_COW)) && (pte & (PTE_W | PTE_COW))) {
        perm = PTE_P | PTE_U | PTE_COW;
        cow = true;
    } else {
        perm = PTE_P | PTE_U;
    }
    r = sys_page_map(father, va, envid, va, perm);
    if (r) { panic("sys_page_map: %e\n", r); }
    if (cow) {
        r = sys_page_map(father, va, father, va, perm);
        if (r) { panic("sys_page_map: %e\n", r); }
    }
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	int r;
	set_pgfault_handler(pgfault);
	envid_t envid = sys_exofork();
	if (envid < 0) { panic("sys_exofork: envid=%e\n", envid); }
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	for (int i = 0; i < NPDENTRIES; i++) {
	    if (uvpd[i] & PTE_P) {
	        for (int j = 0; j < NPTENTRIES; j++) {
                unsigned pn = (unsigned)i * NPTENTRIES + j;
	            uintptr_t va = pn * PGSIZE;
	            if (va >= UTOP) { goto end; }
                if (va == UXSTACKTOP - PGSIZE) { continue; }
	            if (uvpt[pn] & PTE_P) { duppage(envid, pn); }
	        }
	    }
	}
end:
    // for exception stack, we should allocate a new page into child
    r = sys_page_alloc(envid, (void*)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
    if (r) { panic("sys_page_alloc: %e\n", r); }
    extern void _pgfault_upcall(void);
    r = sys_env_set_pgfault_upcall(envid, (void*)_pgfault_upcall);
    if (r) { panic("sys_env_set_pgfault_upcall: %e\n", r); }
	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if (r) { panic("sys_env_set_status: %e\n", r); }
	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
