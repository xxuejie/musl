#define a_barrier a_barrier
static inline void a_barrier()
{
	__asm__ __volatile__ ("fence rw,rw" : : : "memory");
}

#define a_cas a_cas
static inline int a_cas(volatile int *p, int t, int s)
{
	// int old, tmp;
	// __asm__ __volatile__ (
	// 	"\n1:	lr.w.aqrl %0, (%2)\n"
	// 	"	bne %0, %3, 1f\n"
	// 	"	sc.w.aqrl %1, %4, (%2)\n"
	// 	"	bnez %1, 1b\n"
	// 	"1:"
	// 	: "=&r"(old), "=&r"(tmp)
	// 	: "r"(p), "r"((long)t), "r"((long)s)
	// 	: "memory");

  // Dummy implementation for single threaded environment
	int old = *p;
	if (old == t) {
	  *p = s;
	}
	return old;
}

#define a_cas_p a_cas_p
static inline void *a_cas_p(volatile void *p, void *t, void *s)
{
	// void *old;
	// int tmp;
	// __asm__ __volatile__ (
	// 	"\n1:	lr.d.aqrl %0, (%2)\n"
	// 	"	bne %0, %3, 1f\n"
	// 	"	sc.d.aqrl %1, %4, (%2)\n"
	// 	"	bnez %1, 1b\n"
	// 	"1:"
	// 	: "=&r"(old), "=&r"(tmp)
	// 	: "r"(p), "r"(t), "r"(s)
	// 	: "memory");

  // Dummy implementation for single threaded environment
	long old = *((long *) p);
	if (old == (long) t) {
	  *((long *) p) = (long) s;
	}
	return (void *) old;
}

#define a_crash a_crash
static inline void a_crash()
{
	// 0xff615f6372617368 means "0xff + hex('a_crash')"
	*(volatile char *)(0xff615f6372617368)=0;
}
