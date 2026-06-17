plugins {
    id("com.android.application")
}

android {
    namespace = "com.yetty.terminal"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.yetty.terminal"
        minSdk = 28
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            // Architecture from env var: arm64-v8a (default) or x86_64
            val androidAbi = System.getenv("ANDROID_ABI") ?: "arm64-v8a"
            abiFilters += listOf(androidAbi)
        }

        externalNativeBuild {
            cmake {
                val webgpuBackend = System.getenv("WEBGPU_BACKEND") ?: "dawn"
                val buildDir = System.getenv("ANDROID_BUILD_DIR") ?: "${rootProject.projectDir.parentFile.parentFile}/build-android"
                val useCcache = System.getenv("USE_CCACHE") == "1"
                val cmakeArgs = mutableListOf(
                    "-DYETTY_ANDROID=ON",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DWEBGPU_BACKEND=${webgpuBackend}",
                    "-DANDROID_BUILD_DIR=${buildDir}",
                    "-DANDROID_STL=c++_static"
                )
                if (useCcache) {
                    cmakeArgs += listOf(
                        "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
                        "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
                    )
                }
                arguments += cmakeArgs
            }
        }
    }

    // Two installable apps from one project: the terminal (yetty) and the
    // ygui feature showcase (ygreeter). Each flavor builds ONLY its own
    // native CMake target, so the APKs stay lean — the terminal's big
    // libyetty.so never ships in the showcase APK, and libygreeter.so never
    // ships in the terminal APK. The launcher is the same NativeActivity in
    // both; the android.app.lib_name meta-data (a manifest placeholder)
    // selects which library it loads.
    flavorDimensions += "program"
    productFlavors {
        create("yetty") {
            dimension = "program"
            // applicationId inherits com.yetty.terminal from defaultConfig.
            manifestPlaceholders["appLibName"] = "yetty"
            manifestPlaceholders["appLabel"] = "yetty"
            // -DYETTY_GRADLE_FLAVOR differs per flavor only to give AGP a
            // distinct externalNativeBuild hash, so the two flavors build into
            // SEPARATE .cxx staging dirs. Without it both variants share one
            // CMake output tree and Gradle aborts on the cross-variant
            // "uses output without declaring dependency" check. CMake ignores it.
            externalNativeBuild { cmake { targets += "yetty"; arguments += "-DYETTY_GRADLE_FLAVOR=yetty" } }
        }
        create("ygreeter") {
            dimension = "program"
            applicationId = "com.yetty.greeter"
            manifestPlaceholders["appLibName"] = "ygreeter"
            manifestPlaceholders["appLabel"] = "ygreeter"
            externalNativeBuild { cmake { targets += "ygreeter"; arguments += "-DYETTY_GRADLE_FLAVOR=ygreeter" } }
        }
        create("yhello") {
            dimension = "program"
            applicationId = "com.yetty.hello"
            manifestPlaceholders["appLibName"] = "yhello"
            manifestPlaceholders["appLabel"] = "yhello"
            externalNativeBuild { cmake { targets += "yhello"; arguments += "-DYETTY_GRADLE_FLAVOR=yhello" } }
        }
    }

    signingConfigs {
        create("release") {
            storeFile = file(System.getProperty("user.home") + "/.android/debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
        debug {
            isDebuggable = true
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=Debug"
                    // WEBGPU_BACKEND and ANDROID_BUILD_DIR already set in defaultConfig
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
            // Redirect CMake build output to ANDROID_BUILD_DIR/cxx
            val buildDir = System.getenv("ANDROID_BUILD_DIR") ?: "${rootProject.projectDir.parentFile.parentFile}/build-android"
            buildStagingDirectory = File(buildDir, "cxx")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    buildFeatures {
        prefab = true
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true  // Extract native libs to lib/ directory
        }
    }

    // Include pre-built libraries and assets from ANDROID_BUILD_DIR and source directories
    val buildDir = System.getenv("ANDROID_BUILD_DIR") ?: "${rootProject.projectDir.parentFile.parentFile}/build-android"
    val projectRoot = rootProject.projectDir.parentFile.parentFile
    val webgpuBackend = System.getenv("WEBGPU_BACKEND") ?: "dawn"
    val libsDir = "dawn-libs"  // Only Dawn is supported
    sourceSets {
        getByName("main") {
            jniLibs.srcDirs(File(buildDir, libsDir), File(buildDir, "jniLibs"))
            assets.srcDirs(
                File(buildDir, "assets"),
                File(projectRoot, "demo/assets")  // Include demo assets only
            )
        }
    }
}

// Belt-and-suspenders on top of the per-flavor CMake `targets`: make sure no
// native lib from the other program leaks in. The showcase has nothing to do
// with the RISC-V VM, so drop libqemu there too.
androidComponents {
    onVariants(selector().withFlavor("program" to "ygreeter")) { variant ->
        variant.packaging.jniLibs.excludes.add("**/libyetty.so")
        variant.packaging.jniLibs.excludes.add("**/libyhello.so")
        variant.packaging.jniLibs.excludes.add("**/libqemu-system-riscv64.so")
    }
    onVariants(selector().withFlavor("program" to "yhello")) { variant ->
        variant.packaging.jniLibs.excludes.add("**/libyetty.so")
        variant.packaging.jniLibs.excludes.add("**/libygreeter.so")
        variant.packaging.jniLibs.excludes.add("**/libqemu-system-riscv64.so")
    }
    onVariants(selector().withFlavor("program" to "yetty")) { variant ->
        variant.packaging.jniLibs.excludes.add("**/libygreeter.so")
        variant.packaging.jniLibs.excludes.add("**/libyhello.so")
    }
}

dependencies {
    // No Java dependencies needed - pure native app
}

// APK is already in the correct directory (ANDROID_BUILD_DIR/app/outputs/apk/)
// No additional copy needed since layout.buildDirectory is set to ANDROID_BUILD_DIR
