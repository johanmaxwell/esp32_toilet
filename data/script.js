let currentStep = 1;

function updateStepIndicator() {
	document.getElementById(
		"stepIndicator"
	).innerText = `Step ${currentStep} of 3`;
}

function nextStep() {
	if (currentStep < 3) {
		document.getElementById(`step-${currentStep}`).classList.remove("active");
		currentStep++;
		document.getElementById(`step-${currentStep}`).classList.add("active");
		updateStepIndicator();
	}
	if (currentStep > 1) {
		document.getElementById("backButton").style.display = "inline-block";
	}
	if (currentStep === 3) {
		document.getElementById("nextButton").style.display = "none";
		document.getElementById("submitButton").style.display = "inline-block";
	}
}

function prevStep() {
	if (currentStep > 1) {
		document.getElementById(`step-${currentStep}`).classList.remove("active");
		currentStep--;
		document.getElementById(`step-${currentStep}`).classList.add("active");
		updateStepIndicator();
	}
	if (currentStep === 1) {
		document.getElementById("backButton").style.display = "none";
	}
	if (currentStep < 3) {
		document.getElementById("nextButton").style.display = "inline-block";
		document.getElementById("submitButton").style.display = "none";
	}
}

function toggleExtraInputs() {
	const extraInputs = document.getElementById("extraInputs");
	const okupansi = document.querySelector("input[name='okupansi']").checked;
	const pengunjung = document.querySelector("input[name='pengunjung']").checked;
	const tisu = document.querySelector("input[name='tisu']").checked;
	const sabun = document.querySelector("input[name='sabun']").checked;
	const bau = document.querySelector("input[name='bau']").checked;

	extraInputs.innerHTML = "";

	if (okupansi || tisu || bau) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <input type="text" id="nomor_toilet" name="nomor_toilet" placeholder="Nomor Toilet" required>
            </div>
        `;
	}

	if (sabun) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <input type="text" id="nomor_dispenser" name="nomor_dispenser" placeholder="Nomor Dispenser" required>
            </div>
        `;
	}

	if (bau) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <select id="is_luar" name="is_luar" required>
                    <option value="ya">Ya</option>
                    <option value="tidak">Tidak</option>
                </select>
            </div>
        `;
	}

	if (okupansi) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <input type="text" id="jarak_deteksi" name="jarak_deteksi" placeholder="Pengaturan Jarak Deteksi (cm)" required>
            </div>
        `;
	}

	if (tisu) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <input type="text" id="berat_tisu" name="berat_tisu" placeholder="Pengaturan Berat Tisu (gram)" required>
            </div>
        `;
	}

	if (pengunjung) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <h4>Sensor Placement</h4>
                <label><input type="radio" name="placement" value="left" required /> Left</label>
                <label><input type="radio" name="placement" value="right" /> Right</label>
            </div>
        `;
	}
}

function toggleExtraInputsEdit() {
	const extraInputs = document.getElementById("extraInputs");
	const okupansi = document.querySelector("input[name='okupansi']").checked;
	const pengunjung = document.querySelector("input[name='pengunjung']").checked;
	const tisu = document.querySelector("input[name='tisu']").checked;
	const sabun = document.querySelector("input[name='sabun']").checked;
	const bau = document.querySelector("input[name='bau']").checked;

	const existingValues = {};
	extraInputs.querySelectorAll("input, select").forEach((input) => {
		existingValues[input.name] = input.value;
	});

	extraInputs.innerHTML = "";

	if (okupansi || tisu || bau) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label for="nomor_toilet">Nomor Toilet</label>
                <input type="text" id="nomor_toilet" name="nomor_toilet" placeholder="Nomor Toilet" required>
            </div>
        `;
	}

	if (sabun) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label for="nomor_dispenser">Nomor Dispenser</label>
                <input type="text" id="nomor_dispenser" name="nomor_dispenser" placeholder="Nomor Dispenser" required>
            </div>
        `;
	}

	if (bau) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label for="is_luar">Sensor Bau di Luar</label>
                <select id="is_luar" name="is_luar" required>
                    <option value="ya">Ya</option>
                    <option value="tidak">Tidak</option>
                </select>
            </div>
        `;
	}

	if (okupansi) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label for="jarak_deteksi">Pengaturan Jarak Deteksi (cm)</label>
                <input type="text" id="jarak_deteksi" name="jarak_deteksi" placeholder="Pengaturan Jarak Deteksi (cm)" required>
            </div>
        `;
	}

	if (tisu) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label for="berat_tisu">Pengaturan Berat Tisu (gram)</label>
                <input type="text" id="berat_tisu" name="berat_tisu" placeholder="Pengaturan Berat Tisu (gram)" required>
            </div>
        `;
	}

	if (pengunjung) {
		extraInputs.innerHTML += `
            <div class="form-group">
                <label>Sensor Placement</label>
                <label><input type="radio" name="placement" value="left" required /> Left</label>
                <label><input type="radio" name="placement" value="right" /> Right</label>
            </div>
        `;
	}

	extraInputs.querySelectorAll("input, select").forEach((input) => {
		if (existingValues[input.name] !== undefined) {
			input.value = existingValues[input.name];
		}
	});
}

function handleSubmit(event) {
	event.preventDefault();

	const form = document.getElementById("configForm");
	const inputs = form.querySelectorAll("input, select");

	// Validate required fields
	for (let input of inputs) {
		if (
			input.hasAttribute("required") &&
			!input.value.trim() &&
			input.closest(".form-group").offsetParent !== null
		) {
			alert(`Please fill out the ${input.placeholder || input.name} field.`);
			input.focus();
			return;
		}
	}

	const companyInput = form.querySelector("input[name='company']");
	const gedungInput = form.querySelector("input[name='gedung']");
	const lokasiInput = form.querySelector("input[name='lokasi']");

	if (companyInput) companyInput.value = toSnakeCase(companyInput.value);
	if (gedungInput) gedungInput.value = toSnakeCase(gedungInput.value);
	if (lokasiInput) lokasiInput.value = toSnakeCase(lokasiInput.value);

	const formData = new FormData(form);
	const params = new URLSearchParams(formData).toString();

	fetch("/submit", {
		method: "POST",
		headers: { "Content-Type": "application/x-www-form-urlencoded" },
		body: params,
	}).then(() => {
		window.location.href = "/data.html";
	});
}

function toSnakeCase(str) {
	return str
		.replace(/([a-z])([A-Z])/g, "$1_$2")
		.replace(/[\s\-]+/g, "_")
		.replace(/[^a-zA-Z0-9_]/g, "")
		.toLowerCase();
}
