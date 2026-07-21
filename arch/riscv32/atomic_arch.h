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

#define a_crash a_crash
static inline void a_crash()
{
	// 0xff617368 means "0xff + hex('ash')"
	*(volatile char *)(0xff617368)=0;
}
