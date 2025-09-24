package com.kolibri.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kolibri.android.ui.theme.KolibriAndroidTheme

class MainActivity : ComponentActivity() {

    private val viewModel by viewModels<KolibriViewModel> {
        KolibriViewModelFactory(BuildConfig.KOLIBRI_BASE_URL)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            KolibriAndroidTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    KolibriApp(viewModel)
                }
            }
        }
    }
}

@Composable
fun KolibriApp(viewModel: KolibriViewModel) {
    KolibriChatScreen(viewModel = viewModel)
}
