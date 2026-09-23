#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void kernel_bitmatrix(int N, const uint64_t* matrix, const uint64_t* mask,
                      uint32_t* result);
void kernel_bitmatrix_reference(int N, const uint64_t* matrix,
                                const uint64_t* mask, uint32_t* result);

#ifdef __cplusplus
}
#endif
