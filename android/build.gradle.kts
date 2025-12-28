// Top-level build file for Yetty Android
plugins {
    id("com.android.application") version "8.2.0" apply false
}

// Redirect build output to build-android at project root
val projectRoot = rootProject.projectDir.parentFile
allprojects {
    layout.buildDirectory.set(File(projectRoot, "build-android/${project.name}"))
}
