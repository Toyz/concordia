#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include "concordia.h"
#include "test_common.h"

struct ThreadData {
    int i;
    int j;
};

// Callback that verifies values during decoding
static cnd_error_t verify_cb(cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) {
    ThreadData* data = (ThreadData*)ctx->user_ptr;
    uint32_t val = 0;

    if (type == OP_IO_U32) {
        val = *(uint32_t*)ptr;
    } else {
        return CND_ERR_OK;
    }

    const char* key_name = cnd_get_key_name(ctx->program, key);
    if (!key_name) return CND_ERR_INVALID_OP;

    if (strcmp(key_name, "x") == 0) {
        if (val != (uint32_t)data->i) return CND_ERR_VALIDATION;
    } else if (strcmp(key_name, "y") == 0) {
        if (val != (uint32_t)data->j) return CND_ERR_VALIDATION;
    }

    return CND_ERR_OK;
}

// Callback that provides values during encoding
static cnd_error_t encode_cb(cnd_vm_ctx* ctx, uint16_t key, uint8_t type, void* ptr) {
    ThreadData* data = (ThreadData*)ctx->user_ptr;
    const char* key_name = cnd_get_key_name(ctx->program, key);
    if (!key_name) return CND_ERR_INVALID_OP;

    if (strcmp(key_name, "x") == 0) {
        *(uint32_t*)ptr = (uint32_t)data->i;
    } else if (strcmp(key_name, "y") == 0) {
        *(uint32_t*)ptr = (uint32_t)data->j;
    }
    return CND_ERR_OK;
}

class ConcurrencyTest : public ConcordiaTest {
protected:
    void SetUp() override {
        ConcordiaTest::SetUp();
        
        // We need a valid IL image.
        // Let's use the compiler to generate it to be safe and easy.
        // Use 'packet' to ensure bytecode is emitted to the global scope.
        const char* source = "packet Point { uint32 x; uint32 y; };";
        CompileAndLoad(source);
    }
};

TEST_F(ConcurrencyTest, ParallelExecution) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i, ITERATIONS_PER_THREAD]() {
            ThreadData tdata;
            tdata.i = i;
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;
                
                cnd_vm_ctx ctx;
                uint8_t buffer[8] = {0}; // 2 * uint32
                
                // Initialize buffer with data for decoding
                // x = i, y = j
                // Little endian
                buffer[0] = i & 0xFF;
                buffer[1] = (i >> 8) & 0xFF;
                buffer[2] = (i >> 16) & 0xFF; buffer[3] = (i >> 24) & 0xFF;
                
                buffer[4] = j & 0xFF;
                buffer[5] = (j >> 8) & 0xFF;
                buffer[6] = (j >> 16) & 0xFF; buffer[7] = (j >> 24) & 0xFF;

                cnd_init(&ctx, CND_MODE_DECODE, &this->program, buffer, sizeof(buffer), verify_cb, &tdata);
                
                cnd_error_t err = cnd_execute(&ctx);
                if (err == CND_ERR_OK) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}

TEST_F(ConcurrencyTest, ParallelEncoding) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i, ITERATIONS_PER_THREAD]() {
            ThreadData tdata;
            tdata.i = i;
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;

                cnd_vm_ctx ctx;
                uint8_t buffer[8] = {0};
                
                cnd_init(&ctx, CND_MODE_ENCODE, &this->program, buffer, sizeof(buffer), encode_cb, &tdata);
                
                cnd_error_t err = cnd_execute(&ctx);
                if (err == CND_ERR_OK) {
                    // Verify buffer content
                    uint32_t x = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
                    uint32_t y = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
                    
                    if (x == (uint32_t)i && y == (uint32_t)j) {
                        success_count++;
                    }
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}

TEST_F(ConcurrencyTest, ParallelRoundTrip) {
    const int NUM_THREADS = 20;
    const int ITERATIONS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> success_count(0);

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([this, &success_count, i, ITERATIONS_PER_THREAD]() {
            ThreadData tdata;
            tdata.i = i;
            
            for (int j = 0; j < ITERATIONS_PER_THREAD; ++j) {
                tdata.j = j;
                
                // Buffer for the round trip
                uint8_t buffer[8] = {0};
                
                // 1. Encode: Write i, j into the buffer
                cnd_vm_ctx ctx_enc;
                cnd_init(&ctx_enc, CND_MODE_ENCODE, &this->program, buffer, sizeof(buffer), encode_cb, &tdata);
                if (cnd_execute(&ctx_enc) != CND_ERR_OK) continue;

                // 2. Decode & Verify: Read from buffer and compare against i, j in tdata
                cnd_vm_ctx ctx_dec;
                cnd_init(&ctx_dec, CND_MODE_DECODE, &this->program, buffer, sizeof(buffer), verify_cb, &tdata);
                
                if (cnd_execute(&ctx_dec) == CND_ERR_OK) {
                    success_count++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count, NUM_THREADS * ITERATIONS_PER_THREAD);
}
