const exactZObserver = new MutationObserver(() => {
    const slider = document.getElementById("modulatorZ");
    if (!slider || document.getElementById("z88button")) return;

    const button = document.createElement("button");
    button.id = "z88button";
    button.textContent = "Set Z = 88";

    button.onclick = () => {
        slider.value = 88;

        const text = document.getElementById("modulatorTextZ");
        if (text) text.innerText = "88";

        if (currentModulatorPos) {
            currentModulatorPos[2] = 88;
        }

        const canvas = document.getElementById("modulator");
        if (canvas) {
            canvas.style.filter = "hue-rotate(88deg)";
        }
    };

    slider.after(button);
});

exactZObserver.observe(document.body, {
    childList: true,
    subtree: true
});
