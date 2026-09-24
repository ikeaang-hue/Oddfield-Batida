// Opens the installed Batida AU's editor the way Logic does (the classic
// AU v2 Cocoa view in a window, audio running) and clicks it with real
// mouse events, checking the plugin reacts.
//
//   swiftc -O -o build/au_view_check tests/au_view_check.swift && build/au_view_check
//
// Run `killall -9 AudioComponentRegistrar` first after rebuilding the AU.

import AppKit
import AudioToolbox
import CoreAudioKit

setvbuf(stdout, nil, _IONBF, 0)
var failures = 0
func check(_ ok: Bool, _ message: String) {
    print((ok ? "  ok    " : "  FAIL  ") + message)
    if !ok { failures += 1 }
}
func fourCC(_ s: String) -> OSType { s.utf8.reduce(0) { ($0 << 8) | OSType($1) } }
func pump(_ seconds: Double) { RunLoop.current.run(until: Date().addingTimeInterval(seconds)) }

let app = NSApplication.shared
app.setActivationPolicy(.regular)

// The AU, as a v2 instance -------------------------------------------------------------
var desc = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice, componentSubType: fourCC("Btda"),
                                     componentManufacturer: fourCC("Ngsp"), componentFlags: 0, componentFlagsMask: 0)
var unit: AudioUnit?
let started = Date()
AudioComponentInstanceNew(AudioComponentFindNext(nil, &desc)!, &unit)
let au = unit!
var maxFrames: UInt32 = 512
AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, 4)
check(AudioUnitInitialize(au) == noErr, "the AU initialises")

func parameter(_ name: String) -> AudioUnitParameterID? {
    var size: UInt32 = 0
    AudioUnitGetPropertyInfo(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, nil)
    var ids = [AudioUnitParameterID](repeating: 0, count: Int(size) / 4)
    AudioUnitGetProperty(au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &ids, &size)
    for id in ids {
        var info = AudioUnitParameterInfo()
        var infoSize = UInt32(MemoryLayout<AudioUnitParameterInfo>.size)
        AudioUnitGetProperty(au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &info, &infoSize)
        if let cf = info.cfNameString?.takeUnretainedValue(), (cf as String) == name { return id }
    }
    return nil
}
func value(_ id: AudioUnitParameterID) -> Float {
    var v: AudioUnitParameterValue = 0
    AudioUnitGetParameter(au, id, kAudioUnitScope_Global, 0, &v)
    return v
}

// Audio running, as in a host.
var rendering = true
let audio = Thread {
    var time = AudioTimeStamp()
    time.mFlags = .sampleTimeValid
    let buffers = AudioBufferList.allocate(maximumBuffers: 2)
    let left = UnsafeMutablePointer<Float>.allocate(capacity: 512), right = UnsafeMutablePointer<Float>.allocate(capacity: 512)
    buffers[0] = AudioBuffer(mNumberChannels: 1, mDataByteSize: 512 * 4, mData: left)
    buffers[1] = AudioBuffer(mNumberChannels: 1, mDataByteSize: 512 * 4, mData: right)
    var flags = AudioUnitRenderActionFlags()
    while rendering {
        AudioUnitRender(au, &flags, &time, 0, 512, buffers.unsafeMutablePointer)
        time.mSampleTime += 512
        Thread.sleep(forTimeInterval: 0.01)
    }
}
audio.start()

// The editor: the AU's Cocoa view factory, in a window -------------------------------------
var viewInfoSize: UInt32 = 0
var writable: DarwinBoolean = false
AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &viewInfoSize, &writable)
let infoPtr = UnsafeMutableRawPointer.allocate(byteCount: Int(viewInfoSize), alignment: 8)
AudioUnitGetProperty(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, infoPtr, &viewInfoSize)
let viewInfo = infoPtr.load(as: AudioUnitCocoaViewInfo.self)
let bundleURL = viewInfo.mCocoaAUViewBundleLocation.takeUnretainedValue() as URL
let className = viewInfo.mCocoaAUViewClass.takeUnretainedValue() as String
guard let bundle = Bundle(url: bundleURL), let factoryClass = bundle.classNamed(className) as? NSObject.Type else {
    fatalError("No Cocoa view factory in \(bundleURL.path)")
}
let factory = factoryClass.init()
let viewSelector = NSSelectorFromString("uiViewForAudioUnit:withSize:")
typealias ViewFunction = @convention(c) (AnyObject, Selector, AudioUnit, NSSize) -> Unmanaged<NSView>?
let make = unsafeBitCast(factory.method(for: viewSelector), to: ViewFunction.self)
let viewStart = Date()
guard let view = make(factory, viewSelector, au, NSSize(width: 980, height: 640))?.takeUnretainedValue() else {
    fatalError("The factory made no view")
}
print(String(format: "  view made in %.0f ms (%.0f ms since the AU was created)", Date().timeIntervalSince(viewStart) * 1000,
             Date().timeIntervalSince(started) * 1000))

let window = NSWindow(contentRect: NSRect(x: 200, y: 200, width: view.frame.width, height: view.frame.height),
                      styleMask: [.titled, .closable], backing: .buffered, defer: false)
window.contentView = NSView(frame: NSRect(origin: .zero, size: view.frame.size))
window.contentView!.addSubview(view)
window.makeKeyAndOrderFront(nil)
app.activate(ignoringOtherApps: true)
pump(1.0)
check(view.frame.size.width >= 980 && view.frame.size.height >= 640, "the view is \(Int(view.frame.width)) x \(Int(view.frame.height))")

// Clicks, through the window's event queue like a real mouse -----------------------------
func post(_ type: NSEvent.EventType, _ x: CGFloat, _ y: CGFloat) {
    // Editor coordinates are top-left; the window's are bottom-left.
    let p = view.convert(NSPoint(x: x, y: view.isFlipped ? y : view.bounds.height - y), to: nil)
    let e = NSEvent.mouseEvent(with: type, location: p, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                               windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1,
                               pressure: type == .leftMouseUp ? 0 : 1)!
    app.postEvent(e, atStart: false)
}
// Pump events the way the app's run loop would.
func runEvents(_ seconds: Double) {
    let until = Date().addingTimeInterval(seconds)
    while Date() < until {
        if let e = app.nextEvent(matching: .any, until: Date().addingTimeInterval(0.01), inMode: .default, dequeue: true) {
            app.sendEvent(e)
        }
    }
}

if let clip = parameter("Safety Clip"), let heat = parameter("XY Heat") {
    let before = value(clip)
    post(.mouseMoved, 804, 337); runEvents(0.1)
    post(.leftMouseDown, 804, 337); runEvents(0.1)
    post(.leftMouseUp, 804, 337); runEvents(0.5)
    check(value(clip) != before, "a click on CLIP toggles Safety Clip (\(before) -> \(value(clip)))")

    let heatBefore = value(heat)
    post(.leftMouseDown, 214, 300); runEvents(0.05)
    for i in 1...10 { post(.leftMouseDragged, 214, 300 - CGFloat(i) * 8); runEvents(0.02) }
    post(.leftMouseUp, 214, 220); runEvents(0.5)
    check(value(heat) > heatBefore + 0.05, "a drag on the XY pad raises Heat (\(heatBefore) -> \(value(heat)))")
} else {
    check(false, "found the Safety Clip and XY Heat parameters")
}

rendering = false
Thread.sleep(forTimeInterval: 0.05)
window.orderOut(nil)
print(failures == 0 ? "\nPASS" : "\nFAIL: \(failures)")
exit(failures == 0 ? 0 : 1)
