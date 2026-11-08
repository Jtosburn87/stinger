plugins {
    id("com.android.application")
}

// The repository root: its CMakeLists.txt builds the app as libjuce_jni.so (see "Android" there).
val repo: File = rootDir.parentFile

// Android's own copy of JUCE 8.0.8 with the Samsung DeX patches (android/juce-patches), kept apart from the
// copy the Windows and Mac apps build with so the computer versions never change. setup-juce.ps1 makes it.
val juce = File(repo, "android/juce")
// The shared dual-screen (Surface Duo) code, used by every app with a Duo layout: C:/Duo, next to this repository,
// or wherever -Pduo=<folder> says.
val duo = File((project.findProperty("duo") as String?) ?: repo.parentFile.resolve("Duo").path)
require(duo.resolve("android/src").exists()) { "The Duo folder wasn't found at $duo (C:/Duo; or pass -Pduo=<folder>)" }

require(juce.resolve("CMakeLists.txt").exists()) { "Android's JUCE not found: run android/setup-juce.ps1 once first" }

// JUCE's Java start-up code: JuceApp starts JUCE (loading libjuce_jni.so) and puts its window in the activity.
// The rest of JUCE's Java is built into the native library.
val juceJava = layout.buildDirectory.dir("generated/juceJava")
val copyJuceJava by tasks.registering(Sync::class) {
    from(juce.resolve("modules/juce_core/native/javacore/init"))
    from(juce.resolve("modules/juce_core/native/javacore/app"))
    into(juceJava)
}

android {
    namespace = "com.leftnoiseaudio.stinger"
    compileSdk = 37
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.leftnoiseaudio.stinger"
        minSdk = 29   // Android 10: JUCE uses its font APIs
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0-alpha"

        // 64-bit ARM: every current phone and tablet. (-Pabi=x86_64 for the emulator.)
        ndk { abiFilters += listOf((project.findProperty("abi") as String?) ?: "arm64-v8a") }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DFETCHCONTENT_SOURCE_DIR_JUCE=" + juce.invariantSeparatorsPath,
                    "-DDUO_DIR=" + duo.invariantSeparatorsPath,
                    "-DANDROID_STL=c++_static",
                    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5")   // JUCE's copy of Oboe asks for an older CMake than 4 accepts
                targets += "Stinger"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = repo.resolve("CMakeLists.txt")
            version = "4.1.2"
        }
    }

    buildTypes {
        // Debug builds are optimised too: unoptimised audio code can't keep up.
        debug {
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")   // installable straight from the APK
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

androidComponents {
    onVariants { variant ->
        variant.sources.java?.addStaticSourceDirectory(juceJava.get().asFile.absolutePath)
        variant.sources.java?.addStaticSourceDirectory(duo.resolve("android/src").absolutePath)
        variant.sources.java?.addStaticSourceDirectory(duo.resolve("juce/java").absolutePath)
    }
}
tasks.named("preBuild") { dependsOn(copyJuceJava) }

dependencies {
    // Where a dual-screen phone's hinge is (Surface Duo): C:/Duo Hinge.java
    implementation("androidx.window:window-java:1.4.0")
    implementation("androidx.core:core:1.13.1")   // its listener type
}
