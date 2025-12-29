pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

// Redirect Gradle cache to build-android
rootProject.projectDir.parentFile.let { projectRoot ->
    gradle.projectsLoaded {
        allprojects {
            layout.buildDirectory.set(File(projectRoot, "build-android/${project.name}"))
        }
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "yetty"
include(":app")
