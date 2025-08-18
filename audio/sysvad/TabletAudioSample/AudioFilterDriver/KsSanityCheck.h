#pragma once
#include <ntddk.h>
#include <ks.h>
#include <ksmedia.h>

EXTERN_C NTSTATUS SanityCheckKsFilterDesc(_In_ const KSFILTER_DESCRIPTOR* D);
