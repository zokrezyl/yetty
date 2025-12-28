plugins {
    id("com.android.application")
}

android {
    namespace = "com.yetty.terminal"
    compileSdk = 34
    ndkVersion = "26.1.10909125"

    defaultConfig {
        applicationId = "com.yetty.terminal"
        minSdk = 26  // Vulkan 1.0 required
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")  // Primary target
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DYETTY_ANDROID=ON",
                    "-DCMAKE_BUILD_TYPE=Release"
                )
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
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
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
            // Redirect CMake build output to build-android/cxx at project root
            buildStagingDirectory = File(rootProject.projectDir.parentFile, "build-android/cxx")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }

    buildFeatures {
        prefab = true
    }
}

dependencies {
    // No Java dependencies needed - pure native app
}
