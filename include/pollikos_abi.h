#ifndef POLLIKOS_ABI_INFO_H
#define POLLIKOS_ABI_INFO_H

/* Shared, fixed-width description returned by the ABI information syscall.
 * Each architecture keeps its existing syscall numbers and register rules;
 * namespace identifies which operation table the caller is using. */
typedef unsigned short pollikos_abi_u16;
typedef unsigned int pollikos_abi_u32;

#define POLLIKOS_ABI_INFO_MIN_SIZE 8u
#define POLLIKOS_ABI_VERSION_MAJOR 1u
#define POLLIKOS_ABI_VERSION_MINOR 0u

#define POLLIKOS_ABI_ARCH_I386 1u
#define POLLIKOS_ABI_ARCH_X86_64 2u

#define POLLIKOS_ABI_TRANSPORT_INT80 1u
#define POLLIKOS_ABI_TRANSPORT_SYSCALL64 2u

#define POLLIKOS_ABI_NAMESPACE_I386 1u
#define POLLIKOS_ABI_NAMESPACE_X86_64 2u

#define POLLIKOS_ABI_FEATURE_PROCESS (1u << 0)
#define POLLIKOS_ABI_FEATURE_FILES   (1u << 1)
#define POLLIKOS_ABI_FEATURE_IPC     (1u << 2)
#define POLLIKOS_ABI_FEATURE_NETWORK (1u << 3)
#define POLLIKOS_ABI_FEATURE_WINDOWS (1u << 4)

#define POLLIKOS_EFAULT 14
#define POLLIKOS_EINVAL 22

typedef struct {
    pollikos_abi_u32 size; /* Bytes known by the kernel, including this field. */
    pollikos_abi_u16 major;
    pollikos_abi_u16 minor;
    pollikos_abi_u32 architecture;
    pollikos_abi_u32 transport;
    pollikos_abi_u32 operation_namespace;
    pollikos_abi_u32 features;
} PollikAbiInfo;

typedef char pollikos_abi_info_size_must_be_24[(sizeof(PollikAbiInfo) == 24) ? 1 : -1];

#endif
