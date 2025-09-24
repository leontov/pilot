package com.kolibri.android

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

class KolibriViewModel(private val nodeClient: KolibriNodeClient) : ViewModel() {

    private val _uiState = MutableStateFlow(KolibriUiState())
    val uiState: StateFlow<KolibriUiState> = _uiState.asStateFlow()

    fun onBaseUrlChange(newUrl: String) {
        nodeClient.updateBaseUrl(newUrl)
        _uiState.update { it.copy(baseUrl = newUrl) }
    }

    fun sendMessage(text: String) {
        if (text.isBlank()) return
        _uiState.update {
            it.copy(
                input = "",
                messages = it.messages + ChatMessage.User(text),
                isLoading = true,
                errorMessage = null
            )
        }
        viewModelScope.launch {
            val currentBaseUrl = _uiState.value.baseUrl
            val result = nodeClient.sendDialogMessage(text, currentBaseUrl)
            _uiState.update { state ->
                result.fold(
                    onSuccess = { response ->
                        state.copy(
                            messages = state.messages + ChatMessage.Bot(response.answer, response.traceSummary()),
                            isLoading = false,
                            errorMessage = null
                        )
                    },
                    onFailure = { error ->
                        state.copy(
                            messages = state.messages + ChatMessage.Error(error.localizedMessage ?: "Не удалось связаться с узлом."),
                            isLoading = false,
                            errorMessage = error.localizedMessage
                        )
                    }
                )
            }
        }
    }

    fun onInputChanged(value: String) {
        _uiState.update { it.copy(input = value) }
    }
}

class KolibriViewModelFactory(private val defaultBaseUrl: String) : ViewModelProvider.Factory {
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(KolibriViewModel::class.java)) {
            val client = KolibriNodeClient(defaultBaseUrl)
            @Suppress("UNCHECKED_CAST")
            return KolibriViewModel(client) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class $modelClass")
    }
}

data class KolibriUiState(
    val messages: List<ChatMessage> = emptyList(),
    val input: String = "",
    val isLoading: Boolean = false,
    val errorMessage: String? = null,
    val baseUrl: String = BuildConfig.KOLIBRI_BASE_URL
)

sealed interface ChatMessage {
    val id: Long

    data class User(val text: String, override val id: Long = nextId()) : ChatMessage
    data class Bot(val text: String, val trace: String?, override val id: Long = nextId()) : ChatMessage
    data class Error(val text: String, override val id: Long = nextId()) : ChatMessage

    companion object {
        private var counter = 0L
        private fun nextId(): Long {
            counter += 1
            return counter
        }
    }
}

private fun DialogResponse.traceSummary(): String? {
    return trace?.takeIf { it.isNotEmpty() }?.joinToString(separator = " → ")
}
