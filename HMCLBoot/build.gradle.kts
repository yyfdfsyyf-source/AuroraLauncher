plugins {
    id("java-library")
}

tasks.compileJava {
    options.release.set(21)
}

tasks.compileTestJava {
    options.release.set(21)
}
