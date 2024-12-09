  function signalStrength(rssi) {
      if (rssi > -50) return 5;
      if (rssi > -60) return 4;
      if (rssi > -70) return 3;
      if (rssi > -80) return 2;
      return 1;
    }

    function refreshNetworks() {
      fetch('/scan').then(response => response.json()).then(data => {
        let list = document.getElementById('wifi-list');
        list.innerHTML = '';
        data.forEach(network => {
          let li = document.createElement('li');
          li.innerHTML = `<span>${network.ssid}</span><span class="signal-icon" data-strength="${signalStrength(network.rssi)}">&#x1F4F6;</span>`;
          li.onclick = () => document.getElementById('ssid').value = network.ssid;
          list.appendChild(li);
        });
      });
    }
    

    function togglePassword(fieldId) {
      const passwordField = document.getElementById(fieldId);
      const toggleIcon = passwordField.nextElementSibling;
    
      if (passwordField.type === 'password') {
        passwordField.type = 'text';
        toggleIcon.classList.replace("toggle-password","toggle-password-alert");
      } else {
        passwordField.type = 'password';
        toggleIcon.classList.replace("toggle-password-alert","toggle-password");
      }
    }
    

    function showNotification(message, success = true) {
    const container = document.querySelector('.container');
    const notification = document.createElement('div');
    notification.style.background = success ? 'lightgreen' : 'salmon';
    notification.style.color = 'black';
    notification.style.padding = '10px';
    notification.style.marginTop = '10px';
    notification.style.borderRadius = '5px';
    notification.innerText = message;
    container.prepend(notification);
    setTimeout(() => notification.remove(), 3000);
  }

  // Lista zapisanych sieci
let savedNetworks = [];

// Pobierz zapisane sieci z serwera
function refreshSavedNetworks() {
  fetch('/get_saved_networks')
    .then(response => response.json())
    .then(data => {
      savedNetworks = data;
      updateSavedNetworksList();
    })
    .catch(error => console.error('Błąd podczas pobierania zapisanych sieci:', error));
}

// Aktualizuj listę zapisanych sieci
function updateSavedNetworksList() {
  const list = document.getElementById('saved-networks-list');
  list.innerHTML = '';

  savedNetworks.forEach((network, index) => {
    const li = document.createElement('li');
    li.textContent = network.ssid;
    li.onclick = () => selectNetwork(index);
    list.appendChild(li);
  });
}

// Wybór zapisanej sieci
function selectNetwork(index) {
  const selectedNetwork = savedNetworks[index];
  document.getElementById('ssid').value = selectedNetwork.ssid;
  document.getElementById('password').value = selectedNetwork.password; // Hasło nie jest wyświetlane
}

// Usuń wybraną sieć
function removeNetwork() {
  const ssid = document.getElementById('ssid').value;

  if (!ssid) {
    alert('Wybierz SSID sieci do usunięcia.');
    return;
  }

  fetch(`/remove_network`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid })
  })
    .then(response => response.text())
    .then(message => {
      alert(message);
      refreshSavedNetworks();
    })
    .catch(error => console.error('Błąd podczas usuwania sieci:', error));
}

// Funkcja do połączenia z Wi-Fi
function connectWiFi() {
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;

  if (!ssid) {
    alert('Wprowadź SSID sieci.');
    return;
  }

  fetch('/connect', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password })
  })
    .then(response => response.text())
    .then(message => showNotification(message, message.includes('Połączono')))
    .catch(error => console.error('Błąd podczas łączenia z siecią Wi-Fi:', error));
}

// Ogólna funkcja zapisywania danych (sieci Wi-Fi, MQTT)
function saveData(endpoint, payload, successMessage) {
  fetch(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  })
    .then(response => response.text())
    .then(message => {
      showNotification(successMessage || message, true);
      if (endpoint === '/save_network') refreshSavedNetworks();
    })
    .catch(error => console.error(`Błąd podczas zapisu danych: ${error}`));
}

function saveNetwork() {
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;

  if (!ssid) {
    alert('Wprowadź SSID sieci.');
    return;
  }

  saveData('/save_network', { ssid, password }, 'Sieć Wi-Fi zapisana.');
}

function saveMQTT() {
  const server = document.getElementById('mqtt_server').value;
  const port = document.getElementById('mqtt_port').value;
  const user = document.getElementById('mqtt_user').value;
  const password = document.getElementById('mqtt_password').value;

  saveData('/mqtt', { server, port, user, password }, 'Ustawienia MQTT zapisane.');
}


    // Odśwież zapisane sieci przy starcie
    refreshSavedNetworks();
    setInterval(refreshNetworks, 5000);
    refreshNetworks();