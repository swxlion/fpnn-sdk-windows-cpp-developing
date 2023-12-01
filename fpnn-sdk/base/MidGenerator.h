#ifndef MID_GEN_H
#define MID_GEN_H
#include <atomic>
#include <string>

class MidGenerator{
    public:
		// Take the last digit of ip4, so that the mid generated inside a network segment will not be repeated.
		// Notice: Up to tens of millions of non-repeated mids can be generated in one second.
		static void init(const std::string& ip4 = "127.0.0.1");
		static void init(int32_t rand = 0);
        static int64_t genMid();

    private:
		static std::atomic<uint32_t> _sn;
		static int32_t _pip;
};

#endif
