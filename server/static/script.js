let state = { decks: [], activeDeckId: null, activeTab: 'cards', activeCards: [] };

async function api(method, path, body) {
  const res = await fetch(path, {
    method,
    headers: { 'Content-Type': 'application/json' },
    body: body ? JSON.stringify(body) : undefined
  });
  
  const data = await res.json();
  
  // Eğer sunucu hata döndürdüyse (ör. Limit aşıldı 400 hatası) UI'a göster ve işlemi kes.
  if (!res.ok) {
    toast(data.error || 'Bir hata oluştu', 'error');
    return null; // Null döndürerek çağıran fonksiyonun devam etmesini engelliyoruz
  }
  return data;
}

function toast(msg, type='success') {
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className = 'toast show ' + type;
  setTimeout(() => el.className = 'toast', 2500);
}

async function loadDecks() {
  const data = await api('GET', '/api/decks');
  if (!data) return;
  state.decks = data.decks || [];
  renderSidebar();
  if (state.activeDeckId) renderMain();
}

function renderSidebar() {
  const el = document.getElementById('deckList');
  if (!state.decks.length) {
    el.innerHTML = '<div style="padding:20px;color:#64748b;font-size:.85rem;text-align:center">No decks yet</div>';
    return;
  }
  el.innerHTML = state.decks.map(d => `
    <div class="deck-item ${d.id === state.activeDeckId ? 'active' : ''}" onclick="selectDeck(${d.id})">
      <span class="deck-name">${d.name}</span>
      <span class="deck-count">${d.card_count}</span>
    </div>
  `).join('');
}

async function selectDeck(id) {
  state.activeDeckId = id;
  state.activeTab = 'cards';
  const data = await api('GET', `/api/deck/${id}`);
  if (!data) return;
  state.activeCards = data.cards || [];
  renderSidebar();
  renderMain();
}

function activeDeck() {
  return state.decks.find(d => d.id === state.activeDeckId);
}

function renderMain() {
  const deck = activeDeck();
  if (!deck) return;
  document.getElementById('deckTitle').textContent = deck.name;
  document.getElementById('deckSubtitle').textContent = deck.card_count + ' cards · Last updated: ' + (deck.updated_at || '-');
  document.getElementById('tabBar').style.display = 'flex';
  const actions = document.getElementById('deckActions');
  actions.innerHTML = '';
  const renameBtn = document.createElement('button');
  renameBtn.className = 'btn btn-ghost btn-sm';
  renameBtn.textContent = '✏️ Rename';
  renameBtn.onclick = () => renameDeck(deck.id);
  const deleteBtn = document.createElement('button');
  deleteBtn.className = 'btn btn-danger btn-sm';
  deleteBtn.textContent = '🗑️ Delete';
  deleteBtn.onclick = () => deleteDeck(deck.id);
  actions.append(renameBtn, deleteBtn);
  switchTab(state.activeTab);
}

function switchTab(tab) {
  state.activeTab = tab;
  document.querySelectorAll('.tab').forEach((t, i) => {
    t.classList.toggle('active', ['cards','import'][i] === tab);
  });
  if (tab === 'cards') renderCardsTab();
  if (tab === 'import') renderImportTab();
}

function renderCardsTab() {
  const cards = state.activeCards;
  const content = document.getElementById('tabContent');
  content.innerHTML = '';

  const form = document.createElement('div');
  form.className = 'add-card-form';
  form.innerHTML = '<h3>Add New Card</h3>';
  const row = document.createElement('div');
  row.className = 'form-row';

  const gQ = document.createElement('div'); gQ.className = 'form-group';
  gQ.innerHTML = '<label>Question</label>';
  const inQ = document.createElement('input'); inQ.id = 'newQ'; inQ.placeholder = "Capital of France?";
  gQ.appendChild(inQ);

  const gA = document.createElement('div'); gA.className = 'form-group';
  gA.innerHTML = '<label>Answer</label>';
  const inA = document.createElement('input'); inA.id = 'newA'; inA.placeholder = 'Paris';
  gA.appendChild(inA);

  const addBtn = document.createElement('button');
  addBtn.className = 'btn btn-primary';
  addBtn.textContent = '+ Add';
  addBtn.onclick = addCard;

  inA.addEventListener('keydown', e => { if (e.key === 'Enter') addCard(); });

  row.append(gQ, gA, addBtn);
  form.appendChild(row);
  content.appendChild(form);

  if (cards.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.innerHTML = '<div class="icon">🃏</div><p>There are no cards in this deck yet</p>';
    content.appendChild(empty);
    return;
  }

  const table = document.createElement('table');
  table.className = 'card-table';
  table.innerHTML = '<thead><tr><th>#</th><th>Question</th><th>Answer</th><th></th></tr></thead>';
  const tbody = document.createElement('tbody');

  cards.forEach((c, i) => {
    const tr = document.createElement('tr');
    const tdN = document.createElement('td'); tdN.style.cssText = 'color:#64748b;width:40px'; tdN.textContent = i+1;
    const tdQ = document.createElement('td'); tdQ.className = 'editable'; tdQ.textContent = c.question;
    tdQ.onclick = () => openEditCard(c.id, c.question, c.answer);
    const tdA = document.createElement('td'); tdA.className = 'editable'; tdA.textContent = c.answer;
    tdA.onclick = () => openEditCard(c.id, c.question, c.answer);
    const tdB = document.createElement('td'); tdB.style.cssText = 'width:60px;text-align:right';
    const btn = document.createElement('button'); btn.className = 'btn btn-danger btn-sm'; btn.textContent = 'Delete';
    btn.onclick = () => deleteCard(c.id);
    tdB.appendChild(btn);
    tr.append(tdN, tdQ, tdA, tdB);
    tbody.appendChild(tr);
  });

  table.appendChild(tbody);
  content.appendChild(table);
}

async function addCard() {
  const q = document.getElementById('newQ').value.trim();
  const a = document.getElementById('newA').value.trim();
  if (!q || !a) { toast('Question and answer can not be empty', 'error'); return; }
  
  const res = await api('POST', `/api/deck/${state.activeDeckId}/cards`, { question: q, answer: a });
  if (!res) return; // Limit aşıldıysa veya hata varsa işlemi kes
  
  state.activeCards.push({ id: res.id, question: q, answer: a });
  document.getElementById('newQ').value = '';
  document.getElementById('newA').value = '';
  toast('Card added ✓');
  await loadDecks();
  renderCardsTab();
}

function openEditCard(id, q, a) {
  document.getElementById('editCardId').value = id;
  document.getElementById('editCardQ').value = q;
  document.getElementById('editCardA').value = a;
  openModal('editCardModal');
}

async function saveEditCard() {
  const id = parseInt(document.getElementById('editCardId').value);
  const q  = document.getElementById('editCardQ').value.trim();
  const a  = document.getElementById('editCardA').value.trim();
  const res = await api('PUT', `/api/card/${id}`, { question: q, answer: a });
  if (!res) return;
  
  const card = state.activeCards.find(c => c.id === id);
  if (card) { card.question = q; card.answer = a; }
  closeModal('editCardModal');
  toast('Card updated ✓');
  renderCardsTab();
}

async function deleteCard(id) {
  if (!confirm('Are you sure you want to delete this card?')) return;
  const res = await api('DELETE', `/api/card/${id}`);
  if (!res) return;
  
  state.activeCards = state.activeCards.filter(c => c.id !== id);
  toast('Card deleted');
  await loadDecks();
  renderCardsTab();
}

function renderImportTab() {
  const content = document.getElementById('tabContent');
  content.innerHTML = '';

  const wrapper = document.createElement('div');
  wrapper.style.maxWidth = '540px';

  const zone = document.createElement('div');
  zone.className = 'drop-zone';
  zone.innerHTML = '<div class="icon">📄</div><p><strong>Drag & drop a CSV file</strong> or click to browse</p><p style="margin-top:6px;font-size:.8rem">Format: question,answer</p>';
  const fileInput = document.createElement('input');
  fileInput.type = 'file'; fileInput.accept = '.csv,.txt'; fileInput.style.display = 'none';
  fileInput.addEventListener('change', () => { if (fileInput.files[0]) parseCSV(fileInput.files[0]); });
  zone.appendChild(fileInput);
  zone.onclick = () => fileInput.click();
  zone.addEventListener('dragover', e => { e.preventDefault(); zone.classList.add('drag-over'); });
  zone.addEventListener('dragleave', () => zone.classList.remove('drag-over'));
  zone.addEventListener('drop', e => { e.preventDefault(); zone.classList.remove('drag-over'); if (e.dataTransfer.files[0]) parseCSV(e.dataTransfer.files[0]); });

  const example = document.createElement('div');
  example.style.cssText = 'margin-top:20px;padding:16px;background:#13151f;border:1px solid #2d3148;border-radius:10px;';
  example.innerHTML = '<p style="font-size:.8rem;color:#64748b;margin-bottom:8px">Example CSV:</p><pre style="font-size:.8rem;color:#a78bfa">question,answer\nCapital of France?,Paris\n2 + 2 =?,4</pre>';

  const preview = document.createElement('div');
  preview.id = 'csvPreview';

  wrapper.append(zone, preview, example);
  content.appendChild(wrapper);
}

function parseCSV(file) {
  const reader = new FileReader();
  reader.onload = e => {
    const lines = e.target.result.split(/\r?\n/).map(l => l.trim()).filter(l => l);
    const cards = [];
    for (const line of lines) {
      const commaIdx = line.indexOf(',');
      if (commaIdx === -1) continue;
      const q = line.substring(0, commaIdx).trim();
      const a = line.substring(commaIdx + 1).trim();
      if (!q || !a) continue;
      if (q.toLowerCase() === 'soru' || q.toLowerCase() === 'question') continue;
      cards.push({ question: q, answer: a });
    }
    if (cards.length === 0) { toast('No valid cards found', 'error'); return; }
    showCSVPreview(cards);
  };
  reader.readAsText(file, 'UTF-8');
}

function showCSVPreview(cards) {
  const preview = document.getElementById('csvPreview');
  preview.innerHTML = '';

  const wrap = document.createElement('div');
  wrap.style.cssText = 'margin-top:16px;padding:16px;background:#13151f;border:1px solid #2d3148;border-radius:10px;';

  const hdr = document.createElement('div');
  hdr.style.cssText = 'display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;';
  const span = document.createElement('span');
  span.style.fontSize = '.875rem';
  span.textContent = cards.length + ' cards found';
  const btn = document.createElement('button');
  btn.className = 'btn btn-success btn-sm';
  btn.textContent = 'Import →';
  btn.onclick = () => importCards(cards);
  hdr.append(span, btn);

  const table = document.createElement('table');
  table.className = 'card-table';
  table.style.fontSize = '.8rem';
  table.innerHTML = '<thead><tr><th>Question</th><th>Answer</th></tr></thead>';
  const tbody = document.createElement('tbody');
  cards.slice(0, 5).forEach(c => {
    const tr = document.createElement('tr');
    const tdQ = document.createElement('td'); tdQ.textContent = c.question;
    const tdA = document.createElement('td'); tdA.textContent = c.answer;
    tr.append(tdQ, tdA); tbody.appendChild(tr);
  });
  if (cards.length > 5) {
    const tr = document.createElement('tr');
    const td = document.createElement('td'); td.colSpan = 2; td.style.color = '#64748b';
    td.textContent = '... and ' + (cards.length - 5) + ' more cards';
    tr.appendChild(td); tbody.appendChild(tr);
  }
  table.appendChild(tbody);
  wrap.append(hdr, table);
  preview.appendChild(wrap);
}

async function importCards(cards) {
  const res = await api('POST', `/api/deck/${state.activeDeckId}/import`, { cards });
  if (!res) return; // Limit aşıldıysa dur
  
  toast(res.imported + ' cards imported ✓');
  const data = await api('GET', `/api/deck/${state.activeDeckId}`);
  if (!data) return;
  state.activeCards = data.cards || [];
  await loadDecks();
  switchTab('cards');
}

function openNewDeckModal() {
  document.getElementById('newDeckName').value = '';
  openModal('newDeckModal');
}

async function createDeck() {
  const name = document.getElementById('newDeckName').value.trim();
  if (!name) { toast('Deck name cannot be empty', 'error'); return; }
  
  const data = await api('POST', '/api/decks', { name });
  if (!data) return; // Limit aşıldıysa modali kapatma ve işlemi kes
  
  closeModal('newDeckModal');
  toast('Deck created ✓');
  await loadDecks();
  selectDeck(data.id);
}

async function renameDeck(id) {
  const deck = state.decks.find(d => d.id === id);
  const name = prompt('New deck name:', deck.name);
  if (!name || name === deck.name) return;
  const res = await api('PUT', `/api/deck/${id}`, { name });
  if (!res) return;
  
  toast('Deck renamed ✓');
  await loadDecks();
  renderMain();
}

async function deleteDeck(id) {
  const deck = state.decks.find(d => d.id === id);
  if (!confirm('"' + deck.name + '" deck will be deleted. Are you sure?')) return;
  const res = await api('DELETE', `/api/deck/${id}`);
  if (!res) return;
  
  state.activeDeckId = null; state.activeCards = [];
  toast('Deck deleted');
  await loadDecks();
  document.getElementById('deckTitle').textContent = 'Select a deck';
  document.getElementById('deckSubtitle').textContent = '';
  document.getElementById('tabBar').style.display = 'none';
  document.getElementById('deckActions').innerHTML = '';
  document.getElementById('tabContent').innerHTML = '<div class="empty"><div class="icon">📚</div><p>Select a deck from the left panel</p></div>';
}

function openModal(id) { document.getElementById(id).classList.add('open'); }
function closeModal(id) { document.getElementById(id).classList.remove('open'); }
document.querySelectorAll('.modal-overlay').forEach(m => {
  m.addEventListener('click', e => { if (e.target === m) m.classList.remove('open'); });
});
document.getElementById('newDeckName').addEventListener('keydown', e => { if (e.key === 'Enter') createDeck(); });

loadDecks();