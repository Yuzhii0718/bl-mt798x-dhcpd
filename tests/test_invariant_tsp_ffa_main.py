import pytest
import struct
import ctypes


# Simulate the vulnerable buffer management logic from tsp_ffa_main.c
# This models the accumulation pattern without bounds checking

MEM_REGION_BUFFER_SIZE = 4096  # Typical internal buffer size


class MemRegionBuffer:
    """Simulates the TSP internal buffer with recv_length accumulation"""
    
    def __init__(self, capacity=MEM_REGION_BUFFER_SIZE):
        self.capacity = capacity
        self.buffer = bytearray(capacity)
        self.recv_length = 0
    
    def copy_fragment_safe(self, fragment_data: bytes) -> bool:
        """Safe version: validates before copying"""
        fragment_size = len(fragment_data)
        if self.recv_length + fragment_size > self.capacity:
            return False  # Reject: would overflow
        self.buffer[self.recv_length:self.recv_length + fragment_size] = fragment_data
        self.recv_length += fragment_size
        return True
    
    def copy_fragment_unsafe(self, fragment_data: bytes) -> None:
        """Unsafe version: mimics the vulnerable code (no bounds check)"""
        fragment_size = len(fragment_data)
        # This is what the vulnerable code does - no validation
        end = self.recv_length + fragment_size
        if end <= self.capacity:
            self.buffer[self.recv_length:end] = fragment_data
        # recv_length still accumulates even if we didn't copy (simulating overflow)
        self.recv_length += fragment_size


def simulate_fragment_accumulation(fragments, buffer_capacity=MEM_REGION_BUFFER_SIZE):
    """
    Simulate the fragment accumulation pattern.
    Returns (total_accumulated, would_overflow, safe_rejected_at)
    """
    recv_length = 0
    safe_rejected_at = None
    
    for i, fragment in enumerate(fragments):
        fragment_size = len(fragment)
        if recv_length + fragment_size > buffer_capacity:
            if safe_rejected_at is None:
                safe_rejected_at = i
        recv_length += fragment_size
    
    would_overflow = recv_length > buffer_capacity
    return recv_length, would_overflow, safe_rejected_at


# Adversarial payloads: list of fragment sequences designed to overflow the buffer
ADVERSARIAL_PAYLOADS = [
    # Single oversized fragment
    {
        "name": "single_oversized_fragment",
        "fragments": [b"A" * (MEM_REGION_BUFFER_SIZE + 1)],
        "description": "Single fragment larger than buffer capacity"
    },
    # Many small fragments that accumulate past buffer size
    {
        "name": "many_small_fragments_overflow",
        "fragments": [b"X" * 256] * 20,  # 256 * 20 = 5120 > 4096
        "description": "Multiple small fragments accumulating past buffer"
    },
    # Exactly at boundary then one more
    {
        "name": "boundary_plus_one",
        "fragments": [b"B" * MEM_REGION_BUFFER_SIZE, b"\x41"],
        "description": "Fill buffer exactly then add one more byte"
    },
    # Fragment with max size value (simulating integer-like large size)
    {
        "name": "near_max_fragment",
        "fragments": [b"C" * (MEM_REGION_BUFFER_SIZE - 1), b"D" * 2],
        "description": "Near-full fragment followed by overflow fragment"
    },
    # Gradual accumulation with final overflow
    {
        "name": "gradual_accumulation_overflow",
        "fragments": [b"E" * 512] * 9,  # 512 * 9 = 4608 > 4096
        "description": "Gradual accumulation exceeding buffer"
    },
    # Two fragments each half buffer size + 1
    {
        "name": "two_half_plus_one_fragments",
        "fragments": [b"F" * (MEM_REGION_BUFFER_SIZE // 2 + 1)] * 2,
        "description": "Two fragments each slightly over half buffer"
    },
    # Fragment claiming zero size but with data (edge case)
    {
        "name": "empty_then_overflow",
        "fragments": [b"", b"G" * (MEM_REGION_BUFFER_SIZE + 100)],
        "description": "Empty fragment followed by oversized fragment"
    },
    # Repeated max-size fragments
    {
        "name": "repeated_max_size",
        "fragments": [b"H" * MEM_REGION_BUFFER_SIZE] * 3,
        "description": "Multiple full-buffer-size fragments"
    },
    # Crafted descriptor-like data with embedded size fields
    {
        "name": "crafted_descriptor_overflow",
        "fragments": [
            struct.pack("<I", 0xFFFFFFFF) + b"\x41" * 252,  # Malicious size field
            b"\x42" * (MEM_REGION_BUFFER_SIZE),
        ],
        "description": "Crafted FF-A descriptor with malicious size fields"
    },
    # Integer overflow attempt: large count * small size
    {
        "name": "integer_overflow_attempt",
        "fragments": [b"I" * 1] * (MEM_REGION_BUFFER_SIZE * 2),
        "description": "Many tiny fragments to overflow via accumulation"
    },
]


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS, ids=[p["name"] for p in ADVERSARIAL_PAYLOADS])
def test_fragment_accumulation_bounds_invariant(payload):
    """
    Invariant: The accumulated recv_length from multiple fragment retrievals
    MUST NEVER exceed the mem_region_buffer capacity. Any fragment that would
    cause recv_length + fragment_size > buffer_capacity MUST be rejected before
    the copy operation occurs. The security boundary must hold under adversarial
    fragment sequences crafted by a compromised normal world OS.
    """
    fragments = payload["fragments"]
    description = payload["description"]
    
    buf_safe = MemRegionBuffer(capacity=MEM_REGION_BUFFER_SIZE)
    
    # INVARIANT 1: Safe implementation must never allow recv_length to exceed capacity
    for fragment in fragments:
        result = buf_safe.copy_fragment_safe(fragment)
        # After each operation, recv_length must never exceed capacity
        assert buf_safe.recv_length <= buf_safe.capacity, (
            f"SECURITY VIOLATION [{description}]: recv_length ({buf_safe.recv_length}) "
            f"exceeded buffer capacity ({buf_safe.capacity}). "
            f"Buffer overflow would occur without bounds checking."
        )
    
    # INVARIANT 2: The buffer contents must not be corrupted beyond capacity
    assert len(buf_safe.buffer) == MEM_REGION_BUFFER_SIZE, (
        f"SECURITY VIOLATION [{description}]: Buffer size changed from {MEM_REGION_BUFFER_SIZE} "
        f"to {len(buf_safe.buffer)}. Buffer integrity compromised."
    )
    
    # INVARIANT 3: Verify that overflow attempts are detected and rejected
    total_input_size = sum(len(f) for f in fragments)
    if total_input_size > MEM_REGION_BUFFER_SIZE:
        # If total input exceeds capacity, safe implementation must have rejected some fragments
        assert buf_safe.recv_length <= buf_safe.capacity, (
            f"SECURITY VIOLATION [{description}]: Adversarial input totaling {total_input_size} bytes "
            f"should have been bounded to {MEM_REGION_BUFFER_SIZE} bytes, "
            f"but recv_length is {buf_safe.recv_length}."
        )
    
    # INVARIANT 4: Simulate accumulation and verify overflow detection
    total_accumulated, would_overflow, safe_rejected_at = simulate_fragment_accumulation(
        fragments, MEM_REGION_BUFFER_SIZE
    )
    
    if would_overflow:
        # If overflow would occur, the safe implementation MUST have stopped accumulation
        assert buf_safe.recv_length <= buf_safe.capacity, (
            f"SECURITY VIOLATION [{description}]: Total accumulated size {total_accumulated} "
            f"would overflow buffer of {MEM_REGION_BUFFER_SIZE}. "
            f"Bounds check must reject at fragment index {safe_rejected_at}."
        )
        assert safe_rejected_at is not None, (
            f"SECURITY VIOLATION [{description}]: Overflow detected but no rejection point found. "
            f"Bounds checking logic is missing."
        )
    
    # INVARIANT 5: Each individual fragment copy must be bounds-checked
    buf_check = MemRegionBuffer(capacity=MEM_REGION_BUFFER_SIZE)
    for i, fragment in enumerate(fragments):
        fragment_size = len(fragment)
        pre_recv_length = buf_check.recv_length
        
        # The check that MUST happen before any memcpy
        would_overflow_this = (pre_recv_length + fragment_size) > buf_check.capacity
        result = buf_check.copy_fragment_safe(fragment)
        
        if would_overflow_this:
            # Fragment MUST be rejected
            assert result is False, (
                f"SECURITY VIOLATION [{description}]: Fragment {i} of size {fragment_size} "
                f"at recv_length={pre_recv_length} would overflow buffer "
                f"(capacity={buf_check.capacity}) but was NOT rejected."
            )
            # recv_length must not have changed after rejection
            assert buf_check.recv_length == pre_recv_length, (
                f"SECURITY VIOLATION [{description}]: recv_length changed after rejected fragment. "
                f"Expected {pre_recv_length}, got {buf_check.recv_length}."
            )
        else:
            # Fragment should be accepted
            assert result is True, (
                f"Fragment {i} of size {fragment_size} at recv_length={pre_recv_length} "
                f"should fit in buffer but was rejected."
            )
            assert buf_check.recv_length == pre_recv_length + fragment_size, (
                f"recv_length not updated correctly after valid fragment copy."
            )


@pytest.mark.parametrize("fragment_size,initial_recv_length", [
    # Boundary conditions
    (0, 0),
    (1, MEM_REGION_BUFFER_SIZE - 1),  # Exactly fills buffer
    (1, MEM_REGION_BUFFER_SIZE),       # Buffer already full
    (MEM_REGION_BUFFER_SIZE, 0),       # Single fragment fills buffer exactly
    (MEM_REGION_BUFFER_SIZE + 1, 0),   # Single fragment one byte too large
    (MEM_REGION_BUFFER_SIZE, 1),       # Would overflow by 1
    (0xFFFF, 0),                        # Large fragment size
    (MEM_REGION_BUFFER_SIZE // 2, MEM_REGION_BUFFER_SIZE // 2 + 1),  # Overflow by 1
    (2**31 - 1, 0),                    # Near max int32
    (2**16, 0),                        # 64KB fragment
])
def test_single_fragment_bounds_check_invariant(fragment_size, initial_recv_length):
    """
    Invariant: For any single fragment copy operation, the pre-copy bounds check
    (recv_length + fragment_size <= buffer_capacity) MUST be evaluated and enforced
    before any data is written to the buffer. This prevents buffer overflow regardless
    of the fragment size or current accumulation state.
    """
    # Clamp fragment_size for practical testing (avoid OOM)
    practical_fragment_size = min(fragment_size, MEM_REGION_BUFFER_SIZE * 2)
    practical_recv_length = min(initial_recv_length, MEM_REGION_BUFFER_SIZE)
    
    fragment_data = b"A" * practical_fragment_size
    
    buf = MemRegionBuffer(capacity=MEM_REGION_BUFFER_SIZE)
    # Simulate pre-existing accumulation
    buf.recv_length = practical_recv_length
    
    would_overflow = (practical_recv_length + practical_fragment_size) > MEM_REGION_BUFFER_SIZE
    
    result = buf.copy_fragment_safe(fragment_data)
    
    # INVARIANT: recv_length must never exceed capacity
    assert buf.recv_length <= buf.capacity, (
        f"SECURITY VIOLATION: recv_length ({buf.recv_length}) exceeded "
        f"buffer capacity ({buf.capacity}). "
        f"Fragment size: {practical_fragment_size}, "
        f"Initial recv_length: {practical_recv_length}"
    )
    
    if would_overflow:
        assert result is False, (
            f"SECURITY VIOLATION: Fragment that would cause overflow "
            f"(recv_length={practical_recv_length} + fragment_size={practical_fragment_size} "
            f"> capacity={MEM_REGION_BUFFER_SIZE}) was NOT rejected."
        )
    
    # Buffer physical size must remain unchanged
    assert len(buf.buffer) == MEM_REGION_BUFFER_SIZE, (
        f"SECURITY VIOLATION: Buffer physical size changed. "
        f"Memory corruption detected."
    )