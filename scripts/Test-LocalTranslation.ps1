param([int]$Port = 18765)
$ErrorActionPreference = 'Stop'
# Synthetic input only: never persist recognized screen text in test output.
$prompt = "<bos><start_of_turn>user`nYou are a professional German (de) to English (en) translator. Your goal is to accurately convey the meaning and nuances of the original German text while adhering to English grammar, vocabulary, and cultural sensitivities.`nProduce only the English translation, without any additional explanations or commentary. Please translate the following German text into English:`n`n`nDie Welt ist voller Wunder.<end_of_turn>`n<start_of_turn>model`n"
$body = @{
  prompt = $prompt
  n_predict = 128
  temperature = 0
  cache_prompt = $true
  stop = @('<end_of_turn>')
} | ConvertTo-Json -Compress
$response = Invoke-RestMethod "http://127.0.0.1:$Port/completion" -Method Post -ContentType 'application/json' -Body ([Text.Encoding]::UTF8.GetBytes($body)) -TimeoutSec 60
if ($response.content.Trim() -ne 'The world is full of wonders.') {
  throw 'Synthetic translation did not match the expected English sentence.'
}
'PASS: local German-to-English inference'
$response.timings | Select-Object prompt_ms,predicted_ms
