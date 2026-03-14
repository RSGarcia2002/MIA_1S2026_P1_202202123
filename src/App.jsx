import { useState, useRef, useEffect, useCallback, useMemo } from "react";

// Extrae TODOS los reportes detectados en un bloque de texto de salida.
// Cada match devuelve { path, name, isImage, id }.
function detectAllReports(outputText) {
  const regex = /generado:\s*(\S+\.(jpg|jpeg|png|svg|pdf|txt))/gi;
  const results = [];
  let match;

  while ((match = regex.exec(outputText)) !== null) {
    const path = match[1];
    const name = path.split("/").pop();
    const isImage = /\.(jpg|jpeg|png|svg|pdf)$/i.test(path);

    results.push({
      path,
      name,
      isImage,
      id: `${path}_${Date.now()}_${Math.random()}`,
    });
  }

  return results;
}

// Extrae el numero de lineas no vacias de los comandos de entrada
function countCommands(text) {
  return text.split("\n").filter((l) => l.trim() && !l.trim().startsWith("#"))
    .length;
}

// Cuenta lineas totales
function countLines(text) {
  if (!text.trim()) return 0;
  return text.split("\n").length;
}

// Muestra el contenido de un archivo .txt de reporte
function ReportText({ url }) {
  const [text, setText] = useState("Cargando...");

  useEffect(() => {
    let cancelled = false;

    fetch(url)
      .then((r) => r.text())
      .then((data) => {
        if (!cancelled) setText(data);
      })
      .catch((e) => {
        if (!cancelled) setText("Error al cargar: " + e.message);
      });

    return () => {
      cancelled = true;
    };
  }, [url]);

  return <pre className="report-text">{text}</pre>;
}

// Tarjeta de reporte en el panel lateral
function ReportCard({ rep, active, onClick }) {
  const typeTag = rep.isImage ? "IMG" : "TXT";
  const typeClass = rep.isImage ? "tag-img" : "tag-txt";

  return (
    <button
      className={`report-card${active ? " active" : ""}`}
      onClick={onClick}
      title={rep.path}
      type="button"
    >
      <span className={`report-tag ${typeClass}`}>{typeTag}</span>
      <span className="report-card-name">{rep.name}</span>
    </button>
  );
}

function QuickAction({ title, onClick }) {
  return (
    <button className="quick-action" type="button" onClick={onClick}>
      {title}
    </button>
  );
}

export default function App() {
  const [input, setInput] = useState("");
  const [output, setOutput] = useState("");
  const [loading, setLoading] = useState(false);
  const [reports, setReports] = useState([]);
  const [activeIdx, setActiveIdx] = useState(null);
  const [backendStatus, setBackendStatus] = useState("idle");

  const fileInputRef = useRef(null);
  const outputRef = useRef(null);
  const inputRef = useRef(null);

  // Auto-scroll del textarea de salida
  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  // Selecciona el ultimo reporte al agregar nuevos
  const addReports = useCallback((newReps) => {
    if (newReps.length === 0) return;

    setReports((prev) => {
      const existingPaths = new Set(prev.map((r) => r.path));
      const filtered = newReps.filter((r) => !existingPaths.has(r.path));
      const updated = [...prev, ...filtered];

      if (updated.length > 0) {
        setActiveIdx(updated.length - 1);
      }

      return updated;
    });
  }, []);

  const execute = async () => {
    if (!input.trim()) return;

    setLoading(true);
    setBackendStatus("connecting");

    try {
      const res = await fetch("/execute", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ commands: input }),
      });

      const data = await res.json();
      const out = data.output || "";

      setOutput(out);
      addReports(detectAllReports(out));
      setBackendStatus("online");
    } catch (e) {
      setOutput(
        "Error de conexion con el backend: " +
          e.message +
          "\n\nVerifica que el servidor este corriendo en el puerto 8080.",
      );
      setBackendStatus("offline");
    }

    setLoading(false);
  };

  const handleKeyDown = (e) => {
    if (e.ctrlKey && e.key === "Enter") {
      execute();
    }
  };

  const loadFile = (e) => {
    const file = e.target.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (ev) => setInput(ev.target.result);
    reader.readAsText(file);
    e.target.value = "";
  };

  const clearAll = () => {
    setOutput("");
    setReports([]);
    setActiveIdx(null);
  };

  const clearInput = () => {
    setInput("");
    inputRef.current?.focus();
  };

  const appendTemplate = (template) => {
    setInput((prev) => {
      const base = prev.trim();
      return base ? `${base}\n\n${template}` : template;
    });
    inputRef.current?.focus();
  };

  const loadDemoCommands = () => {
    setInput(`# Demo basica EXTREAMFS

mkdisk -size=10 -unit=M -path=/home/randall/demo.mia
fdisk -size=5 -unit=M -path=/home/randall/demo.mia -name=Part1
mount -path=/home/randall/demo.mia -name=Part1
mounted
mkfs -id=231A
login -user=root -pass=123 -id=231A
mkdir -p -path=/home/proyectos/mia
mkfile -path=/home/proyectos/mia/readme.txt -size=64
cat -file1=/home/proyectos/mia/readme.txt
rep -id=231A -path=/home/randall/reporte_mbr.jpg -name=mbr
rep -id=231A -path=/home/randall/reporte_tree.png -name=tree`);
    inputRef.current?.focus();
  };

  const activeReport = activeIdx !== null ? reports[activeIdx] : null;

  const activeUrl = activeReport
    ? "/report?path=" + encodeURIComponent(activeReport.path)
    : null;

  const commandCount = useMemo(() => countCommands(input), [input]);
  const inputLines = useMemo(() => countLines(input), [input]);
  const outputLines = useMemo(() => countLines(output), [output]);

  const statusLabel =
    backendStatus === "online"
      ? "Backend conectado"
      : backendStatus === "offline"
        ? "Backend desconectado"
        : backendStatus === "connecting"
          ? "Conectando..."
          : "Sin probar conexion";

  return (
    <div className="app-shell">
      <div className="app">
        {/* Cabecera */}
        <header className="header">
          <div className="header-left">
            <div className="brand-mark">EF</div>

            <div className="header-title">
              <h1>EXTREAMFS</h1>
              <span className="header-sub">
                EXT2 Filesystem Simulator • Randall García • 202202123
              </span>
            </div>
          </div>

          <div className="header-right">
            <div className={`status-pill status-${backendStatus}`}>
              <span className="status-dot" />
              {statusLabel}
            </div>

            <div className="header-hint">Ctrl + Enter</div>
          </div>
        </header>

        {/* Barra secundaria */}
        <section className="toolbar">
          <div className="toolbar-group">
            <QuickAction title="Demo rapida" onClick={loadDemoCommands} />
            <QuickAction
              title="Insertar MKDISK"
              onClick={() =>
                appendTemplate(
                  `mkdisk -size=10 -unit=M -path=/home/randall/disco.mia`,
                )}
            />
            <QuickAction
              title="Insertar MOUNT"
              onClick={() =>
                appendTemplate(
                  `mount -path=/home/randall/disco.mia -name=Part1`,
                )}
            />
            <QuickAction
              title="Insertar REP"
              onClick={() =>
                appendTemplate(
                  `rep -id=231A -path=/home/randall/reporte.jpg -name=mbr`,
                )}
            />
          </div>

          <div className="toolbar-meta">
            <span>{commandCount} comandos</span>
            <span>{inputLines} lineas</span>
          </div>
        </section>

        {/* Paneles principales */}
        <div className="workspace">
          {/* Entrada */}
          <section className="panel panel-editor">
            <div className="panel-header">
              <div className="panel-heading">
                <span className="panel-title">Editor de comandos</span>
                <span className="panel-subtitle">
                  Escribe scripts .smia o comandos individuales
                </span>
              </div>

              <div className="btn-group">
                <button
                  className="btn btn-secondary"
                  type="button"
                  onClick={() => fileInputRef.current?.click()}
                >
                  Cargar archivo
                </button>

                <input
                  ref={fileInputRef}
                  type="file"
                  accept=".smia,.mia,.txt"
                  onChange={loadFile}
                  style={{ display: "none" }}
                />

                <button
                  className="btn btn-primary"
                  type="button"
                  onClick={execute}
                  disabled={loading}
                >
                  {loading ? "Ejecutando..." : "Ejecutar"}
                </button>
              </div>
            </div>

            <div className="editor-wrap">
              <textarea
                ref={inputRef}
                className="code-area input-area"
                value={input}
                onChange={(e) => setInput(e.target.value)}
                onKeyDown={handleKeyDown}
                placeholder={`# Script MIA
mkdisk -size=50 -unit=M -path=/home/randall/disco.mia
fdisk -size=10 -unit=M -path=/home/randall/disco.mia -name=Part1
mount -path=/home/randall/disco.mia -name=Part1
mkfs -id=231A
login -user=root -pass=123 -id=231A
rep -id=231A -path=/tmp/mbr.jpg -name=mbr`}
                spellCheck={false}
              />
            </div>

            <div className="panel-footer">
              <div className="panel-footer-left">
                <span>{commandCount} comandos detectados</span>
              </div>

              <div className="panel-footer-right">
                <button
                  className="btn-link"
                  type="button"
                  onClick={clearInput}
                >
                  Limpiar editor
                </button>
              </div>
            </div>
          </section>

          {/* Salida */}
          <section className="panel panel-output">
            <div className="panel-header">
              <div className="panel-heading">
                <span className="panel-title">Terminal de salida</span>
                <span className="panel-subtitle">
                  Resultado de la ejecucion del backend
                </span>
              </div>

              <div className="btn-group">
                <button
                  className="btn btn-secondary"
                  type="button"
                  onClick={clearAll}
                  title="Limpiar salida y reportes"
                >
                  Limpiar todo
                </button>
              </div>
            </div>

            <div className="editor-wrap">
              <textarea
                ref={outputRef}
                className="code-area output-area"
                value={output}
                readOnly
                placeholder="Los resultados apareceran aqui..."
                spellCheck={false}
              />
            </div>

            <div className="panel-footer">
              <div className="panel-footer-left">
                <span>{output ? `${outputLines} lineas` : "Sin salida"}</span>
              </div>

              <div className="panel-footer-right">
                {reports.length > 0 && (
                  <span className="badge">
                    {reports.length} reporte{reports.length !== 1 ? "s" : ""}
                  </span>
                )}
              </div>
            </div>
          </section>
        </div>

        {/* Panel de reportes */}
        <section className={`reports-shell ${reports.length > 0 ? "show" : ""}`}>
          <div className="reports-topbar">
            <div>
              <h2>Explorador de reportes</h2>
              <p>
                Visualiza imagenes y archivos de texto generados durante la
                ejecucion
              </p>
            </div>

            {reports.length > 0 && (
              <button
                className="btn btn-secondary"
                type="button"
                onClick={() => {
                  setReports([]);
                  setActiveIdx(null);
                }}
              >
                Limpiar reportes
              </button>
            )}
          </div>

          {reports.length > 0 ? (
            <div className="reports-section">
              {/* Lista lateral */}
              <div className="reports-list">
                <div className="reports-list-header">
                  <span>Reportes ({reports.length})</span>
                </div>

                <div className="reports-list-body">
                  {reports.map((rep, idx) => (
                    <ReportCard
                      key={rep.id}
                      rep={rep}
                      active={idx === activeIdx}
                      onClick={() => setActiveIdx(idx)}
                    />
                  ))}
                </div>
              </div>

              {/* Visor */}
              <div className="reports-viewer">
                {activeReport ? (
                  <>
                    <div className="reports-viewer-header">
                      <div className="reports-viewer-meta">
                        <span
                          className="reports-viewer-name"
                          title={activeReport.name}
                        >
                          {activeReport.name}
                        </span>

                        <span
                          className="reports-viewer-path"
                          title={activeReport.path}
                        >
                          {activeReport.path}
                        </span>
                      </div>

                      <div className="btn-group">
                        <a
                          className="btn btn-secondary"
                          href={activeUrl}
                          download={activeReport.name}
                        >
                          Descargar
                        </a>

                        <a
                          className="btn btn-secondary"
                          href={activeUrl}
                          target="_blank"
                          rel="noreferrer"
                        >
                          Abrir
                        </a>
                      </div>
                    </div>

                    {activeReport.isImage ? (
                      <div className="report-image-wrap">
                        <img
                          src={activeUrl + "&t=" + Date.now()}
                          alt={activeReport.name}
                          className="report-image"
                        />
                      </div>
                    ) : (
                      <ReportText url={activeUrl} />
                    )}
                  </>
                ) : (
                  <div className="reports-viewer-empty">
                    Selecciona un reporte para verlo aqui
                  </div>
                )}
              </div>
            </div>
          ) : (
            <div className="reports-empty-state">
              <div className="reports-empty-card">
                <h3>No hay reportes generados</h3>
                <p>
                  Ejecuta comandos <code>rep</code> para que aparezcan aqui tus
                  vistas previas.
                </p>
              </div>
            </div>
          )}
        </section>

        <footer className="footer">
          <div className="footer-block">
            <span className="footer-label">Backend</span>
            <code>./build/MIA_P1 --server 8080</code>
          </div>

          <div className="footer-block">
            <span className="footer-label">Frontend</span>
            <code>npm run dev</code>
          </div>

          <div className="footer-block">
            <span className="footer-label">API</span>
            <code>POST /execute</code>
            <code>GET /report?path=</code>
          </div>
        </footer>
      </div>
    </div>
  );
}