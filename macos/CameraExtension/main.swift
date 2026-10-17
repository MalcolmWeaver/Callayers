import Foundation
import CoreMediaIO

let camera = CameraProvider()
CMIOExtensionProvider.startService(provider: camera.provider)
CFRunLoopRun()
