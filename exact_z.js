// Exact Z input for fools25.js

const exactZObserver = new MutationObserver(() => {
    const z = document.getElementById("modulatorZ");

    if (z && z.type === "range") {
        z.type = "number";
        z.min = "1";
        z.max = "500";
        z.step = "1";
        z.style.width = "6em";

        z.oninput = function () {
            const value = Math.max(1, Math.min(500, Number(this.value) || 1));
            
            const text = document.getElementById("modulatorTextZ");
            if (text) text.innerText = value;

            const modulator = document.getElementById("modulator");
            if (modulator) {
                modulator.style.filter =
                    `hue-rotate(${value % 359}deg)`;
            }
        };
    }
});

exactZObserver.observe(document.documentElement, {
    childList: true,
    subtree: true
});
