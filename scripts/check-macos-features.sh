#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$root/build/macos-arm64}"
mkdir -p "$build" "$build/ModuleCache"
export CLANG_MODULE_CACHE_PATH="$build/ModuleCache"
export SWIFT_MODULECACHE_PATH="$build/ModuleCache"
models=("$root/macos/App/WorkspaceModels.swift" "$root/macos/App/AnnotationModels.swift")
auth=("$root/macos/Services/BackendClient.swift" "$root/macos/Services/AuthCallback.swift" "$root/macos/Services/SessionKeychain.swift" "$root/macos/Services/AuthController.swift")
generation=("$root/macos/Services/GenerationPermit.swift" "$root/macos/Services/GenerationContract.swift" "$root/macos/Services/GenerationController.swift")
check() {
    local name="$1"; shift
    xcrun --sdk macosx swiftc -swift-version 5 -parse-as-library "$@" "$root/macos/Tests/$name.swift" -o "$build/$name"
    "$build/$name"
}
check DomainChecks "${models[@]}" "$root/macos/App/CardRevision.swift"
check SaveFlowChecks "${models[@]}" "$root/macos/App/WorkspaceSaveFlow.swift" "$root/macos/Services/GenerationPermit.swift"
check ImageChecks "${models[@]}" "$root/macos/App/ImageImport.swift"
check AuthChecks "${auth[@]}"
check AuthSessionChecks "${models[@]}" "${auth[@]}" "${generation[@]}"
check GenerationChecks "${models[@]}" "$root/macos/Services/BackendClient.swift" "$root/macos/Services/GenerationContract.swift"
check AnnotationChecks "${models[@]}" "$root/macos/App/AnnotationController.swift" "$root/macos/Video/AnnotationRenderer.swift"
check TelemetryChecks "${models[@]}" "$root/macos/Services/TelemetryQueue.swift"
check DeliveryChecks "${models[@]}" "$root/macos/Video/FrameDeliveryTracker.swift"
check CallbackLiveChecks "$root/macos/Services/BackendClient.swift" "$root/macos/Services/AuthCallback.swift"
