#pragma once

// Physical memory. Hands out frames and, for managers that support it, picks a
// page to evict. The caller (MemorySystem) owns the page table and TLB, so it is
// responsible for unmapping/invalidating whatever evict_page() returns.
//
// Fault path:
//     if(!fm.has_free()){ int v = fm.evict_page(); /* unmap v, invalidate TLB */ }
//     int f = fm.allocate(vpn);
class FrameManager{
public:
    virtual ~FrameManager() = default;

    // True if allocate() can be called without evicting first.
    virtual bool has_free() const = 0;

    // Gives vpn a frame. Requires has_free().
    virtual int allocate(int vpn) = 0;

    // Releases a frame and returns the VPN that was in it.
    // The caller must unmap that VPN everywhere.
    virtual int evict_page() = 0;

    // Reports that the page in this frame was accessed (hit or fault). frame must be resident.
    virtual void on_access(int frame) = 0;

    // Number of distinct frames ever used (stat A).
    virtual int frames_used() const = 0;
};
