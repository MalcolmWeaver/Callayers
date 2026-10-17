#!/usr/bin/env swift
import Foundation
import CryptoKit
import Darwin

// Sparkle's current key-file format is a base64-encoded 32-byte Ed25519 seed.
// Prints only the public key. --generate refuses to replace an existing key.
let arguments = CommandLine.arguments
if arguments.count != 3 || !["--public", "--generate"].contains(arguments[1]) {
    fputs("Usage: swift scripts/macos-update-key.swift --public|--generate KEY_FILE\n", stderr); exit(2)
}
let path = URL(fileURLWithPath: arguments[2])
do {
    let key: Curve25519.Signing.PrivateKey
    if arguments[1] == "--generate" {
        key = Curve25519.Signing.PrivateKey()
        let data = Data(key.rawRepresentation.base64EncodedString().utf8)
        let descriptor = open(path.path, O_WRONLY | O_CREAT | O_EXCL, 0o600)
        guard descriptor >= 0 else { throw CocoaError(.fileWriteFileExists) }
        let handle = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
        try handle.write(contentsOf: data); try handle.synchronize(); try handle.close()
    } else {
        let raw = try String(contentsOf: path, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)
        guard let data = Data(base64Encoded: raw), data.count == 32 else { throw CocoaError(.fileReadCorruptFile) }
        key = try Curve25519.Signing.PrivateKey(rawRepresentation: data)
    }
    print(key.publicKey.rawRepresentation.base64EncodedString())
} catch { fputs("Unable to read or create the update signing key.\n", stderr); exit(1) }
