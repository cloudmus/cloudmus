# 1. Защита внутренней структуры коллекций Kotlin
-keep class kotlin.collections.** { *; }
-dontwarn kotlin.collections.**

# 2. Защита компонентов инициализации и жизненного цикла AndroidX
-keep class androidx.lifecycle.** { *; }
-keep class androidx.startup.** { *; }
-dontwarn androidx.lifecycle.**
-dontwarn androidx.startup.**

# 3. Сохранение сигнатур и аннотаций для корректной работы OkHttp и Coil
-keepattributes *Annotation*, Signature, InnerClasses, EnclosingMethod
