package com.kolibri.android

import com.squareup.moshi.Json
import com.squareup.moshi.JsonClass
import com.squareup.moshi.Moshi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.logging.HttpLoggingInterceptor
import java.io.IOException

class KolibriNodeClient(defaultBaseUrl: String) {

    private val moshi = Moshi.Builder().build()
    private val requestAdapter = moshi.adapter(DialogRequest::class.java)
    private val responseAdapter = moshi.adapter(DialogResponse::class.java)

    private val client: OkHttpClient = OkHttpClient.Builder()
        .addInterceptor(
            HttpLoggingInterceptor().apply {
                setLevel(HttpLoggingInterceptor.Level.BASIC)
            }
        )
        .build()

    private val mediaTypeJson = "application/json".toMediaType()

    private val baseUrlFlow = MutableBaseUrl(defaultBaseUrl)

    suspend fun sendDialogMessage(prompt: String, baseUrlOverride: String?): Result<DialogResponse> {
        return runCatching {
            withContext(Dispatchers.IO) {
                val baseUrl = baseUrlOverride?.takeIf { it.isNotBlank() } ?: baseUrlFlow.current
                val url = baseUrl.trimEnd('/') + "/api/v1/dialog"
                val bodyJson = requestAdapter.toJson(DialogRequest(prompt))
                val request = Request.Builder()
                    .url(url)
                    .post(bodyJson.toRequestBody(mediaTypeJson))
                    .build()
                client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) {
                        throw IOException("HTTP ${'$'}{response.code}: ${'$'}{response.message}")
                    }
                    val responseBody = response.body?.string() ?: throw IOException("Empty response body")
                    responseAdapter.fromJson(responseBody) ?: throw IOException("Failed to parse response")
                }
            }
        }
    }

    fun updateBaseUrl(newUrl: String) {
        baseUrlFlow.current = newUrl
    }
}

private class MutableBaseUrl(initial: String) {
    @Volatile
    var current: String = initial
}

@JsonClass(generateAdapter = true)
data class DialogRequest(
    @Json(name = "input") val input: String
)

@JsonClass(generateAdapter = true)
data class DialogResponse(
    @Json(name = "answer") val answer: String,
    @Json(name = "trace") val trace: List<String>? = null
)
