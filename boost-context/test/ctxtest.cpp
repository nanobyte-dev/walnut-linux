// Boost.Context on ppc64: fiber ping-pong, callee-saved registers across switches (GPRs, FPRs,
// AltiVec), asio spawn with yield_context (what wesnothd uses), Boost.Coroutine (v1) and
// Boost.Fiber. Prints PASS/FAIL per test; exit status = number of failures.
#include <boost/context/fiber.hpp>
#include <boost/context/continuation.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/detached.hpp>
#include <boost/coroutine/asymmetric_coroutine.hpp>
#include <boost/fiber/all.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include <stdexcept>
#ifdef __ALTIVEC__
#include <altivec.h>
#endif

namespace ctx = boost::context;
static int failures = 0;
static void report(const char *name, bool ok) {
	std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
	if (!ok) failures++;
}

// 1. ping-pong: values passed back and forth, many switches
static void test_pingpong() {
	long sum = 0;
	long n = 0;
	ctx::fiber f{[&](ctx::fiber &&main) {
		for (;;) { n++; main = std::move(main).resume(); }
		return std::move(main);
	}};
	for (int i = 0; i < 100000; i++) { f = std::move(f).resume(); sum += n; }
	report("fiber ping-pong (100000 switches)", n == 100000 && sum == 100000L * 100001 / 2);
}

// 2. floating point values live across resume() in both contexts. The ABI says f14-f31 survive
// a call; if the switch doesn't keep them, one side sees the other's values.
__attribute__((noinline)) static double fp_work(ctx::fiber &other, double seed, int rounds) {
	double a = seed, b = seed * 2, c = seed * 3, d = seed * 5, e = seed * 7, g = seed * 11;
	double h = seed * 13, k = seed * 17, m = seed * 19, p = seed * 23, q = seed * 29, r = seed * 31;
	for (int i = 0; i < rounds; i++) {
		other = std::move(other).resume();	// a..r live across the call
		a += 1.0; b += a; c += b; d += c; e += d; g += e; h += g; k += h; m += k; p += m; q += p; r += q;
	}
	return a + b + c + d + e + g + h + k + m + p + q + r;
}
static double fp_expect(double seed, int rounds) {
	volatile double s = seed;	// same arithmetic without switches
	double a = s, b = s * 2, c = s * 3, d = s * 5, e = s * 7, g = s * 11;
	double h = s * 13, k = s * 17, m = s * 19, p = s * 23, q = s * 29, r = s * 31;
	for (int i = 0; i < rounds; i++) {
		a += 1.0; b += a; c += b; d += c; e += d; g += e; h += g; k += h; m += k; p += m; q += p; r += q;
	}
	return a + b + c + d + e + g + h + k + m + p + q + r;
}
static void test_fp() {
	double in_fiber = 0;
	ctx::fiber f{[&](ctx::fiber &&main) {
		in_fiber = fp_work(main, 1000.5, 50);
		return std::move(main);
	}};
	double in_main = fp_work(f, 0.25, 50);	// the fiber ran 50 rounds; one more resume ends it
	if (f) f = std::move(f).resume();
	bool ok = in_main == fp_expect(0.25, 50) && in_fiber == fp_expect(1000.5, 50);
	if (!ok) std::printf("  main %.17g (want %.17g), fiber %.17g (want %.17g)\n",
		in_main, fp_expect(0.25, 50), in_fiber, fp_expect(1000.5, 50));
	report("FPRs (f14-f31) kept across resume", ok);
}

// 2b. the same with integer callee-saved registers (r14-r31)
__attribute__((noinline)) static long gpr_work(ctx::fiber &other, long seed, int rounds) {
	long a = seed, b = seed * 2, c = seed * 3, d = seed * 5, e = seed * 7, g = seed * 11;
	long h = seed * 13, k = seed * 17, m = seed * 19, p = seed * 23, q = seed * 29, r = seed * 31;
	for (int i = 0; i < rounds; i++) {
		other = std::move(other).resume();
		a += 1; b ^= a; c += b; d ^= c; e += d; g ^= e; h += g; k ^= h; m += k; p ^= m; q += p; r ^= q;
	}
	return a + b + c + d + e + g + h + k + m + p + q + r;
}
static long gpr_expect(long seed, int rounds) {
	volatile long s = seed;
	long a = s, b = s * 2, c = s * 3, d = s * 5, e = s * 7, g = s * 11;
	long h = s * 13, k = s * 17, m = s * 19, p = s * 23, q = s * 29, r = s * 31;
	for (int i = 0; i < rounds; i++) {
		a += 1; b ^= a; c += b; d ^= c; e += d; g ^= e; h += g; k ^= h; m += k; p ^= m; q += p; r ^= q;
	}
	return a + b + c + d + e + g + h + k + m + p + q + r;
}
static void test_gpr() {
	long in_fiber = 0;
	ctx::fiber f{[&](ctx::fiber &&main) {
		in_fiber = gpr_work(main, 1000003, 50);
		return std::move(main);
	}};
	long in_main = gpr_work(f, 7, 50);
	if (f) f = std::move(f).resume();
	report("GPRs (r14-r31) kept across resume", in_main == gpr_expect(7, 50) && in_fiber == gpr_expect(1000003, 50));
}

#ifdef __ALTIVEC__
// 2c. AltiVec v20-v31 (callee-saved in the 64-bit ELF ABI)
typedef __vector float vf;
__attribute__((noinline)) static float vmx_work(ctx::fiber &other, float seed, int rounds) {
	vf one = {1, 1, 1, 1};
	vf a = vec_splats(seed), b = a + a, c = b + a, d = c + a, e = d + a, g = e + a;
	vf h = g + a, k = h + a, m = k + a, p = m + a, q = p + a, r = q + a;
	for (int i = 0; i < rounds; i++) {
		other = std::move(other).resume();
		a += one; b += a; c += b; d += c; e += d; g += e; h += g; k += h; m += k; p += m; q += p; r += q;
	}
	vf s = a + b + c + d + e + g + h + k + m + p + q + r;
	return s[0] + s[1] + s[2] + s[3];
}
static float vmx_expect(float seed, int rounds) {
	volatile float sv = seed;
	float a = sv, b = a + a, c = b + a, d = c + a, e = d + a, g = e + a;
	float h = g + a, k = h + a, m = k + a, p = m + a, q = p + a, r = q + a;
	for (int i = 0; i < rounds; i++) {
		a += 1; b += a; c += b; d += c; e += d; g += e; h += g; k += h; m += k; p += m; q += p; r += q;
	}
	return 4 * (a + b + c + d + e + g + h + k + m + p + q + r);
}
static void test_vmx() {
	float in_fiber = 0;
	ctx::fiber f{[&](ctx::fiber &&main) {
		in_fiber = vmx_work(main, 3.0f, 20);
		return std::move(main);
	}};
	float in_main = vmx_work(f, 0.5f, 20);
	if (f) f = std::move(f).resume();
	bool ok = in_main == vmx_expect(0.5f, 20) && in_fiber == vmx_expect(3.0f, 20);
	if (!ok) std::printf("  main %g (want %g), fiber %g (want %g)\n", in_main, vmx_expect(0.5f, 20), in_fiber, vmx_expect(3.0f, 20));
	report("AltiVec (v20-v31) kept across resume", ok);
}
#endif

// 3. exceptions thrown and caught inside a fiber, and unwinding a suspended fiber
static void test_exceptions() {
	bool caught = false, unwound = false;
	{
		ctx::fiber f{[&](ctx::fiber &&main) {
			try { throw std::runtime_error("in fiber"); } catch (const std::exception &) { caught = true; }
			struct guard { bool &u; ~guard() { u = true; } } gd{unwound};
			main = std::move(main).resume();	// never resumed: destroyed suspended
			return std::move(main);
		}};
		f = std::move(f).resume();
	}	// ~fiber unwinds the suspended stack (ontop_fcontext)
	report("exceptions in a fiber, unwinding a suspended fiber", caught && unwound);
}

// 4. callcc / continuation
static void test_callcc() {
	int x = 0;
	ctx::continuation c = ctx::callcc([&](ctx::continuation &&m) {
		for (int i = 0; i < 10; i++) { x++; m = m.resume(); }
		return std::move(m);
	});
	while (c) c = c.resume();
	report("callcc/continuation", x == 10);
}

// 5. asio spawn with yield_context and timers (wesnothd's pattern)
static void test_asio() {
	boost::asio::io_context io;
	std::vector<int> order;
	for (int id = 0; id < 3; id++) {
		boost::asio::spawn(io, [&io, &order, id](boost::asio::yield_context yield) {
			boost::asio::steady_timer t(io);
			for (int i = 0; i < 3; i++) {
				t.expires_after(std::chrono::milliseconds(5 + id));
				t.async_wait(yield);
				order.push_back(id);
			}
		}, boost::asio::detached);
	}
	io.run();
	report("asio::spawn + yield_context + steady_timer", order.size() == 9);
}

// 6. Boost.Coroutine (v1), libboost_coroutine
static void test_coroutine() {
	typedef boost::coroutines::asymmetric_coroutine<int> coro;
	int sum = 0;
	coro::pull_type source([](coro::push_type &sink) { for (int i = 1; i <= 100; i++) sink(i); });
	for (int v : source) sum += v;
	report("Boost.Coroutine asymmetric_coroutine", sum == 5050);
}

// 7. Boost.Fiber, libboost_fiber
static void test_boost_fiber() {
	int n = 0;
	boost::fibers::fiber a([&] { for (int i = 0; i < 5; i++) { n++; boost::this_fiber::yield(); } });
	boost::fibers::fiber b([&] { for (int i = 0; i < 5; i++) { n += 10; boost::this_fiber::yield(); } });
	a.join(); b.join();
	report("Boost.Fiber fibers + yield + join", n == 55);
}

int main() {
	test_pingpong();
	test_gpr();
	test_fp();
#ifdef __ALTIVEC__
	test_vmx();
#endif
	test_exceptions();
	test_callcc();
	test_asio();
	test_coroutine();
	test_boost_fiber();
	std::printf("%d failure(s)\n", failures);
	return failures;
}
