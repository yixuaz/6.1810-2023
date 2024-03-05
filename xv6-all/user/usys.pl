#!/usr/bin/perl -w

# Generate usys.S, the stubs for syscalls.

print "# generated by usys.pl - do not edit\n";

print "#include \"kernel/syscall.h\"\n";

sub entry {
    my $name = shift;
    print ".global $name\n";
    print "${name}:\n";
    print " li a7, SYS_${name}\n";
    print " ecall\n";
    print " ret\n";
}
	
entry("fork");
entry("exit");
entry("wait");
entry("pipe");
entry("read");
entry("write");
entry("close");
entry("kill");
entry("exec");
entry("open");
entry("mknod");
entry("unlink");
entry("fstat");
entry("link");
entry("mkdir");
entry("chdir");
entry("dup");
entry("getpid");
entry("sbrk");
entry("sleep");
entry("uptime");
entry("mmap");
entry("munmap");
entry("svprint");
entry("trace");
entry("sysinfo");
entry("sbrknaive");
entry("pgaccess");
entry("dirtypages");
entry("vmprint");
entry("sigalarm");
entry("sigreturn");
entry("testbacktrace");
entry("clone");
entry("join");
entry("connect");
entry("recvfrom");
entry("sendto");
entry("accept");
entry("symlink");
entry("sigsend");
entry("signal");
entry("sigprocmask");
