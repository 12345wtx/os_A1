#include "ksignal.h"

#include <defs.h>
#include <proc.h>
#include <trap.h>

/**
 * @brief init the signal struct inside a PCB.
 * 
 * @param p 
 * @return int 
 */
int siginit(struct proc *p) {
    // Initialize signal struct
    memset(&p->signal, 0, sizeof(struct ksignal));
    
    // Set default actions for all signals
    for (int i = SIGMIN; i <= SIGMAX; i++) {
        p->signal.sa[i].sa_sigaction = SIG_DFL;
        p->signal.sa[i].sa_mask = 0;
        p->signal.sa[i].sa_restorer = NULL;
    }
    
    // Initialize signal mask and pending signals
    p->signal.sigmask = 0;
    p->signal.sigpending = 0;
    
    return 0;
}

int siginit_fork(struct proc *parent, struct proc *child) {
    // Copy parent's signal actions and mask
    memcpy(&child->signal.sa, &parent->signal.sa, sizeof(sigaction_t) * (SIGMAX + 1));
    child->signal.sigmask = parent->signal.sigmask;
    
    // Clear pending signals for child
    child->signal.sigpending = 0;
    
    return 0;
}

int siginit_exec(struct proc *p) {
    // Keep signal mask and pending signals
    sigset_t mask = p->signal.sigmask;
    sigset_t pending = p->signal.sigpending;
    
    // Reset all signal actions to default except ignored ones
    for (int i = SIGMIN; i <= SIGMAX; i++) {
        if (p->signal.sa[i].sa_sigaction != SIG_IGN) {
            p->signal.sa[i].sa_sigaction = SIG_DFL;
            p->signal.sa[i].sa_mask = 0;
            p->signal.sa[i].sa_restorer = NULL;
        }
    }
    
    // Restore mask and pending signals
    p->signal.sigmask = mask;
    p->signal.sigpending = pending;
    
    return 0;
}

int do_signal(void) {
    struct proc *p = curr_proc();
    struct trapframe *tf = p->trapframe;
    
    // Check for pending signals that are not blocked
    sigset_t pending = p->signal.sigpending & ~p->signal.sigmask;
    
    if (pending == 0)
        return 0;
        
    // Find lowest numbered pending signal
    int signo;
    for (signo = SIGMIN; signo <= SIGMAX; signo++) {
        if (pending & sigmask(signo))
            break;
    }
    
    // Clear the pending signal
    p->signal.sigpending &= ~sigmask(signo);
    
    // Get signal action
    sigaction_t *sa = &p->signal.sa[signo];
    
    // Handle signal based on action
    if (sa->sa_sigaction == SIG_IGN) {
        // Ignore signal
        return 0;
    } else if (sa->sa_sigaction == SIG_DFL) {
        // Default action - terminate process
        setkilled(p, -10 - signo);
        return 0;
    } else {
        // Custom handler
        
        // Save current context on user stack
        struct ucontext uc;
        uc.uc_sigmask = p->signal.sigmask;
        uc.uc_mcontext.epc = tf->epc;
        
        // Save all registers
        uc.uc_mcontext.regs[0] = tf->ra;
        uc.uc_mcontext.regs[1] = tf->sp;
        uc.uc_mcontext.regs[2] = tf->gp;
        uc.uc_mcontext.regs[3] = tf->tp;
        uc.uc_mcontext.regs[4] = tf->t0;
        uc.uc_mcontext.regs[5] = tf->t1;
        uc.uc_mcontext.regs[6] = tf->t2;
        uc.uc_mcontext.regs[7] = tf->s0;
        uc.uc_mcontext.regs[8] = tf->s1;
        uc.uc_mcontext.regs[9] = tf->a0;
        uc.uc_mcontext.regs[10] = tf->a1;
        uc.uc_mcontext.regs[11] = tf->a2;
        uc.uc_mcontext.regs[12] = tf->a3;
        uc.uc_mcontext.regs[13] = tf->a4;
        uc.uc_mcontext.regs[14] = tf->a5;
        uc.uc_mcontext.regs[15] = tf->a6;
        uc.uc_mcontext.regs[16] = tf->a7;
        uc.uc_mcontext.regs[17] = tf->s2;
        uc.uc_mcontext.regs[18] = tf->s3;
        uc.uc_mcontext.regs[19] = tf->s4;
        uc.uc_mcontext.regs[20] = tf->s5;
        uc.uc_mcontext.regs[21] = tf->s6;
        uc.uc_mcontext.regs[22] = tf->s7;
        uc.uc_mcontext.regs[23] = tf->s8;
        uc.uc_mcontext.regs[24] = tf->s9;
        uc.uc_mcontext.regs[25] = tf->s10;
        uc.uc_mcontext.regs[26] = tf->s11;
        uc.uc_mcontext.regs[27] = tf->t3;
        uc.uc_mcontext.regs[28] = tf->t4;
        uc.uc_mcontext.regs[29] = tf->t5;
        uc.uc_mcontext.regs[30] = tf->t6;
        
        // Set up signal handler arguments
        tf->a0 = signo;  // signo
        tf->a1 = tf->sp - sizeof(siginfo_t);  // siginfo_t pointer
        tf->a2 = tf->sp - sizeof(siginfo_t) - sizeof(struct ucontext);  // ucontext pointer
        
        // Save context and siginfo on user stack
        tf->sp -= sizeof(siginfo_t) + sizeof(struct ucontext);
        if (copy_to_user(p->mm, tf->sp, (char*)&uc, sizeof(struct ucontext)) < 0)
            return -EFAULT;
        if (copy_to_user(p->mm, tf->sp + sizeof(struct ucontext), (char*)&p->signal.siginfos[signo], sizeof(siginfo_t)) < 0)
            return -EFAULT;
            
        // Set up signal handler return
        tf->ra = (uint64)sa->sa_restorer;
        
        // Set new signal mask
        p->signal.sigmask |= sa->sa_mask | sigmask(signo);
        
        // Jump to signal handler
        tf->epc = (uint64)sa->sa_sigaction;
    }
    
    return 0;
}

// syscall handlers:
//  sys_* functions are called by syscall.c

int sys_sigaction(int signo, const sigaction_t __user *act, sigaction_t __user *oldact) {
    struct proc *p = curr_proc();
    
    // Check if signal number is valid
    if (signo < SIGMIN || signo > SIGMAX)
        return -EINVAL;
        
    // SIGKILL and SIGSTOP cannot be caught or ignored
    if (signo == SIGKILL || signo == SIGSTOP)
        return -EINVAL;
        
    // Save old action if requested
    if (oldact) {
        if (copy_to_user(p->mm, (uint64)oldact, (char*)&p->signal.sa[signo], sizeof(sigaction_t)) < 0)
            return -EFAULT;
    }
    
    // Set new action if provided
    if (act) {
        if (copy_from_user(p->mm, (char*)&p->signal.sa[signo], (uint64)act, sizeof(sigaction_t)) < 0)
            return -EFAULT;
    }
    
    return 0;
}

int sys_sigreturn() {
    struct proc *p = curr_proc();
    struct trapframe *tf = p->trapframe;
    
    // Get saved context from user stack
    struct ucontext uc;
    if (copy_from_user(p->mm, (char*)&uc, tf->sp, sizeof(struct ucontext)) < 0)
        return -EFAULT;
        
    // Restore registers
    tf->epc = uc.uc_mcontext.epc;
    tf->ra = uc.uc_mcontext.regs[0];
    tf->sp = uc.uc_mcontext.regs[1];
    tf->gp = uc.uc_mcontext.regs[2];
    tf->tp = uc.uc_mcontext.regs[3];
    tf->t0 = uc.uc_mcontext.regs[4];
    tf->t1 = uc.uc_mcontext.regs[5];
    tf->t2 = uc.uc_mcontext.regs[6];
    tf->s0 = uc.uc_mcontext.regs[7];
    tf->s1 = uc.uc_mcontext.regs[8];
    tf->a0 = uc.uc_mcontext.regs[9];
    tf->a1 = uc.uc_mcontext.regs[10];
    tf->a2 = uc.uc_mcontext.regs[11];
    tf->a3 = uc.uc_mcontext.regs[12];
    tf->a4 = uc.uc_mcontext.regs[13];
    tf->a5 = uc.uc_mcontext.regs[14];
    tf->a6 = uc.uc_mcontext.regs[15];
    tf->a7 = uc.uc_mcontext.regs[16];
    tf->s2 = uc.uc_mcontext.regs[17];
    tf->s3 = uc.uc_mcontext.regs[18];
    tf->s4 = uc.uc_mcontext.regs[19];
    tf->s5 = uc.uc_mcontext.regs[20];
    tf->s6 = uc.uc_mcontext.regs[21];
    tf->s7 = uc.uc_mcontext.regs[22];
    tf->s8 = uc.uc_mcontext.regs[23];
    tf->s9 = uc.uc_mcontext.regs[24];
    tf->s10 = uc.uc_mcontext.regs[25];
    tf->s11 = uc.uc_mcontext.regs[26];
    tf->t3 = uc.uc_mcontext.regs[27];
    tf->t4 = uc.uc_mcontext.regs[28];
    tf->t5 = uc.uc_mcontext.regs[29];
    tf->t6 = uc.uc_mcontext.regs[30];
    
    // Restore signal mask
    p->signal.sigmask = uc.uc_sigmask;
    
    // Restore stack pointer
    tf->sp += sizeof(struct ucontext) + sizeof(siginfo_t);
    
    return 0;
}

int sys_sigprocmask(int how, const sigset_t __user *set, sigset_t __user *oldset) {
    return 0;
}

int sys_sigpending(sigset_t __user *set) {
    return 0;
}

int sys_sigkill(int pid, int signo, int code) {
    struct proc *p;
    
    // Check if signal number is valid
    if (signo < SIGMIN || signo > SIGMAX)
        return -EINVAL;
        
    // Find target process
    for (int i = 0; i < NPROC; i++) {
        p = pool[i];
        acquire(&p->lock);
        if (p->pid == pid) {
            // Set signal as pending
            p->signal.sigpending |= sigmask(signo);
            
            // For SIGKILL, terminate process immediately
            if (signo == SIGKILL) {
                setkilled(p, -10 - signo);
            }
            
            // Wake up process if sleeping
            if (p->state == SLEEPING) {
                p->state = RUNNABLE;
                add_task(p);
            }
            
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    
    return -EINVAL;
}