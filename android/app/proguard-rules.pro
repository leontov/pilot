# Keep Moshi generated adapters and Kotlin metadata for reflection.
-keep class kotlin.Metadata { *; }
-keepclassmembers class ** {
    @com.squareup.moshi.Json(name=*) <fields>;
}
-dontwarn okio.**
