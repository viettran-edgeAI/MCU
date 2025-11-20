#pragma once

const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Feedback Trainer</title>
    <style>
        body { font-family: Arial, sans-serif; background: #0e131a; color: #f7f7f7; margin: 0; padding: 20px; }
        .card { background: #191f2b; padding: 20px; border-radius: 12px; max-width: 980px; margin: 20px auto; box-shadow: 0 10px 30px rgba(0,0,0,0.45); }
        .card.subtle { background: #121722; box-shadow: none; border: 1px solid #1f2735; }
        h1 { color: #4CAF50; margin-top: 0; }
        label { display: block; margin-bottom: 6px; font-weight: bold; }
        input { width: 100%; padding: 10px; border-radius: 6px; border: none; margin-bottom: 12px; background: #0d1118; color: #fff; }
        button { background: #4CAF50; color: white; border: none; border-radius: 6px; padding: 12px 18px; font-size: 1em; cursor: pointer; margin: 4px; transition: opacity 0.2s; }
        button.secondary { background: #2196F3; }
        button.danger { background: #f44336; }
        button.outline { background: transparent; border: 1px solid #4CAF50; }
        button:disabled { opacity: 0.5; cursor: not-allowed; }
        img { width: 100%; max-width: 640px; border-radius: 12px; margin-top: 16px; border: 3px solid #2d3545; box-shadow: 0 6px 24px rgba(0,0,0,0.45); }
        .status { margin-top: 10px; font-size: 0.92em; color: #cbd5f5; }
        .prediction { font-size: 2.4em; color: #4CAF50; margin: 10px 0; letter-spacing: 0.04em; }
        .row { display: flex; flex-wrap: wrap; gap: 16px; }
        .col { flex: 1; min-width: 320px; }
        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 12px; margin-top: 16px; }
        .stat-card { background: #1f2633; border-radius: 10px; padding: 14px; }
        .stat-label { text-transform: uppercase; font-size: 0.75em; color: #93a3c7; letter-spacing: 0.1em; }
        .stat-value { font-size: 1.8em; font-weight: bold; margin-top: 6px; color: #f7f7f7; }
    .class-grid { margin-top: 18px; display: flex; flex-wrap: wrap; gap: 10px; }
    .class-pill { padding: 8px 14px; border-radius: 999px; border: 1px solid #4CAF50; background: #0d1118; font-size: 0.9em; }
    .class-pill.active { background: #4CAF50; color: #060909; box-shadow: 0 0 8px rgba(76, 175, 80, 0.5); }
        #feedback-panel { display: none; margin-top: 16px; padding: 16px; }
        #feedback-panel input { margin-bottom: 8px; }
        #feedback-panel button { margin-top: 4px; }
        .subtext { font-size: 0.85em; color: #8ea0c8; }
    </style>
</head>
<body>
    <div class="card" id="config-card">
        <h1>Configure Session</h1>
        <label>Model name</label>
        <input type="text" id="model" placeholder="enter_model_name" value="model_name">
        <label>Inference interval (ms)</label>
        <input type="number" id="interval" min="200" value="1500">
        <button id="start-btn">Start Inference</button>
        <div class="status" id="config-status"></div>
    </div>

    <div class="card" id="inference-card" style="display:none;">
        <div class="row">
            <div class="col">
                <h1>Live Stream</h1>
                <img id="stream" alt="Video stream">
                <div class="class-grid" id="class-list">
                    <div class="class-pill">Classes will appear here</div>
                </div>
            </div>
            <div class="col">
                <h1>Inference</h1>
                <div>Prediction:</div>
                <div class="prediction" id="pred">--</div>
                <div>Inference time: <span id="time">--</span> μs</div>
                <div class="status" id="state"></div>
                <div class="stats-grid">
                    <div class="stat-card">
                        <div class="stat-label">Total Predictions</div>
                        <div class="stat-value" id="total-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Auto Confirmed</div>
                        <div class="stat-value" id="auto-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Manual Corrections</div>
                        <div class="stat-value" id="manual-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Wrong Predictions</div>
                        <div class="stat-value" id="wrong-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Model Accuracy</div>
                        <div class="stat-value" id="accuracy-label">--</div>
                    </div>
                </div>
                <div class="status" id="accuracy-status">Awaiting training...</div>
                <button id="wrong-btn" class="secondary">Wrong Prediction</button>
                <button id="retrain-btn">Retrain Model</button>
                <button id="finish-btn" class="danger">Finish Session</button>
                <div id="feedback-panel" class="card subtle">
                    <label>Enter correct label</label>
                    <input type="text" id="feedback-label" placeholder="actual label">
                    <button id="submit-feedback" class="secondary">Submit Label</button>
                    <button id="cancel-feedback" class="outline">Cancel</button>
                    <div class="subtext">Inference is paused while waiting for your confirmation.</div>
                </div>
            </div>
        </div>
    </div>

<script>
const configCard = document.getElementById('config-card');
const inferenceCard = document.getElementById('inference-card');
const configStatus = document.getElementById('config-status');
const predEl = document.getElementById('pred');
const timeEl = document.getElementById('time');
const stateEl = document.getElementById('state');
const totalCountEl = document.getElementById('total-count');
const wrongBtn = document.getElementById('wrong-btn');
const retrainBtn = document.getElementById('retrain-btn');
const finishBtn = document.getElementById('finish-btn');
const feedbackPanel = document.getElementById('feedback-panel');
const feedbackInput = document.getElementById('feedback-label');
const submitFeedbackBtn = document.getElementById('submit-feedback');
const cancelFeedbackBtn = document.getElementById('cancel-feedback');
const streamImg = document.getElementById('stream');
const autoCountEl = document.getElementById('auto-count');
const manualCountEl = document.getElementById('manual-count');
const wrongCountEl = document.getElementById('wrong-count');
const accuracyLabel = document.getElementById('accuracy-label');
const accuracyStatus = document.getElementById('accuracy-status');
const classListEl = document.getElementById('class-list');

let streamLoaded = false;
let awaitingCorrection = false;

function setStream() {
    if (streamLoaded) return;
    const host = window.location.hostname || window.location.host.split(':')[0];
    const isIPv6 = host.includes(':');
    const base = window.location.protocol + '//' + (isIPv6 ? '[' + host + ']' : host);
    streamImg.src = base + ':81/stream';
    streamLoaded = true;
}

document.getElementById('start-btn').onclick = () => {
    const model = document.getElementById('model').value.trim();
    const interval = document.getElementById('interval').value.trim();
    configStatus.textContent = 'Configuring...';
    fetch(`/configure?model=${encodeURIComponent(model)}&interval=${encodeURIComponent(interval)}`)
        .then(r => r.json())
        .then(data => { configStatus.textContent = data.result || data.error || JSON.stringify(data); })
        .catch(err => configStatus.textContent = err.message);
};

wrongBtn.onclick = () => {
    fetch('/feedback/start').then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = true;
        feedbackPanel.style.display = 'block';
        feedbackInput.value = '';
        feedbackInput.focus();
    });
};

submitFeedbackBtn.onclick = () => {
    const label = feedbackInput.value.trim();
    if (!label) {
        alert('Please enter the correct label');
        return;
    }
    fetch(`/feedback/submit?label=${encodeURIComponent(label)}`).then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = false;
        feedbackPanel.style.display = 'none';
    });
};

cancelFeedbackBtn.onclick = () => {
    fetch('/feedback/cancel').then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = false;
        feedbackPanel.style.display = 'none';
    });
};

retrainBtn.onclick = () => {
    fetch('/retrain').then(r => r.json()).then(data => {
        alert(data.result || data.error);
    });
};

finishBtn.onclick = () => {
    fetch('/finish').then(r => r.json()).then(data => {
        alert(data.result || data.error);
        if (!data.error) {
            inferenceCard.style.display = 'none';
            configCard.style.display = 'block';
            configStatus.textContent = 'Session finished. Refresh or reset the board to start over.';
            streamImg.src = '';
            streamLoaded = false;
        }
    });
};

function renderClasses(list, prediction) {
    if (!classListEl) return;
    classListEl.innerHTML = '';
    if (!list || !list.length) {
        const placeholder = document.createElement('div');
        placeholder.className = 'class-pill';
        placeholder.textContent = 'No classes available';
        classListEl.appendChild(placeholder);
        return;
    }
    for (const cls of list) {
        const pill = document.createElement('div');
        pill.className = 'class-pill';
        pill.textContent = cls;
        if (cls === prediction) {
            pill.classList.add('active');
        }
        classListEl.appendChild(pill);
    }
}

function refreshStatus() {
    fetch(`/status?ts=${Date.now()}`)
        .then(r => r.json())
        .then(data => {
            if (data.session_finished) {
                configCard.style.display = 'block';
                inferenceCard.style.display = 'none';
                configStatus.textContent = 'Session finished. Refresh or reset to start again.';
                streamImg.src = '';
                streamLoaded = false;
                return;
            }
            if (!data.configured) {
                configCard.style.display = 'block';
                inferenceCard.style.display = 'none';
                return;
            }
            configCard.style.display = 'none';
            inferenceCard.style.display = 'block';
            setStream();

            predEl.textContent = data.prediction;
            timeEl.textContent = data.inference_time_us;
            stateEl.textContent = data.message;
            totalCountEl.textContent = data.total_predictions;
            manualCountEl.textContent = data.manual_corrections ?? data.completed_feedback ?? 0;
            autoCountEl.textContent = data.auto_confirmations ?? 0;
            wrongCountEl.textContent = data.wrong_predictions ?? 0;
            const accuracyVal = parseFloat(data.last_training_accuracy);
            if (!isNaN(accuracyVal) && accuracyVal >= 0) {
                const pct = (accuracyVal * 100).toFixed(2) + '%';
                accuracyLabel.textContent = pct;
                accuracyStatus.textContent = 'Last training accuracy: ' + pct;
            } else {
                accuracyLabel.textContent = '--';
                accuracyStatus.textContent = 'Awaiting training...';
            }
            renderClasses(data.classes || [], data.prediction);

            if (data.feedback_state === 'AwaitingCorrection') {
                awaitingCorrection = true;
                feedbackPanel.style.display = 'block';
            } else if (!awaitingCorrection) {
                feedbackPanel.style.display = 'none';
            }
        })
        .catch(err => console.log(err));
}

setInterval(refreshStatus, 700);
refreshStatus();
</script>
</body>
</html>
)rawliteral";

