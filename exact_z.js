// Exact Z Input for the Sandboxels Element Modulator
// Use together with fools25.js

runAfterLoad(function () {
    const observer = new MutationObserver(function () {
        const z = document.getElementById("modulatorZ");

        if (z && z.type === "range") {
            z.type = "number";
            z.min = "1";
            z.max = "500";
            z.step = "1";
            z.style.width = "6em";

            z.oninput = function () {
                let value = Number(this.value);

                if (value < 1) value = 1;
                if (value > 500) value = 500;

                document.getElementById("modulatorTextZ").innerText = value;
                document.getElementById("modulator").style.filter =
                    "hue-rotate(" + (value % 359) + "deg)";
            };
        }
    });

    observer.observe(document.body, {
        childList: true,
        subtree: true
    });
});
