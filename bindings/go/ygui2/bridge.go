// Callback bridges — the //export half of the ygui2 package. cgo
// requires exported functions to live in a file whose preamble contains
// declarations only, so the bridges are split out of bindings.go (whose
// preamble defines the static helpers).
//
// Every native callback lands here with an id in userdata (converted
// pointer<->integer in C, never as a Go uintptr cast) and dispatches to
// the closure registered under that id. The closures own the panic
// guard, so nothing can unwind across the cgo boundary.
package ygui2

// #include <stdint.h>
// #include <stddef.h>
// struct yetty_yclass_object;
// static uint64_t yetty_bridge_ptr_to_id(void *userdata) { return (uint64_t)(uintptr_t)userdata; }
import "C"

import "unsafe"

//export yettyGoYgui2Click
func yettyGoYgui2Click(widget *C.struct_yetty_yclass_object, userdata unsafe.Pointer) {
	if callback := lookupClick(uint64(C.yetty_bridge_ptr_to_id(userdata))); callback != nil {
		callback()
	}
}

//export yettyGoYgui2Select
func yettyGoYgui2Select(widget *C.struct_yetty_yclass_object, index C.uint32_t,
	userdata unsafe.Pointer) {
	if callback := lookupSelect(uint64(C.yetty_bridge_ptr_to_id(userdata))); callback != nil {
		callback(int(index))
	}
}

//export yettyGoYgui2Key
func yettyGoYgui2Key(key C.uint32_t, mods C.uint32_t, userdata unsafe.Pointer) C.int {
	if callback := lookupKey(uint64(C.yetty_bridge_ptr_to_id(userdata))); callback != nil {
		return C.int(callback(uint32(key), uint32(mods)))
	}
	return 0
}

//export yettyGoYgui2Sink
func yettyGoYgui2Sink(bytes *C.uint8_t, byteCount C.size_t, userdata unsafe.Pointer) {
	if callback := lookupSink(uint64(C.yetty_bridge_ptr_to_id(userdata))); callback != nil {
		callback(C.GoBytes(unsafe.Pointer(bytes), C.int(byteCount)))
	}
}
