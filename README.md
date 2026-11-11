# OLED Desktop Mode (Windows x64)

Nas definições da aplicação, a opção **Iniciar automaticamente com o Windows** cria ou remove o atalho de arranque da sessão atual. O motor abre na área de notificação, sem abrir as definições.

O código original deste projeto é distribuído sob a [licença MIT](LICENSE). O SDK AMD ADLX é uma dependência externa, não incluída neste repositório, e mantém os seus próprios termos de licença.

Para instalar no Windows, executar `build/installer/OLED-Desktop-Mode-Setup.exe`. O instalador cria a entrada em Aplicações instaladas e o atalho no menu Iniciar, com opção de arranque automático. A instalação é por utilizador e reutiliza os perfis existentes.

Para gerar o instalador, executar `build.ps1` e compilar `installer/OledDesktopMode.iss` com o Inno Setup 6. O ícone próprio é incorporado nos executáveis e usado na área de notificação.

Aplicação C++ que altera diretamente AMD Display Custom Color através de ADLX. Não usa filtros, uma janela sobre o wallpaper, automação do Adrenalin, registo privado do driver ou reinício do AMD Software.

## Usar

Executar `build\OledDesktopMode.exe`. Abre a janela de definições e mantém o motor na área de notificação. Um clique no ícone volta a abrir as definições; botão direito permite abrir, ativar/desativar ou sair e restaurar. Fechar a janela de definições mantém a deteção automática ativa. Não requer administrador, não instala um serviço nem modifica o arranque do Windows.

## Ajustar o modo OLED e interface Windows 11

A janela oferece seleção de monitor, sliders e valores numéricos para Brilho, Contraste, Saturação, Matiz e Temperatura de cor. Os limites e passos são consultados no driver AMD; uma propriedade não suportada fica desativada. «Repor perfil OLED» coloca os valores iniciais nos campos; «Guardar perfil» guarda o perfil desse monitor. O estado normal nunca é substituído pelo perfil editado. Mudar de monitor ou fechar com alterações por guardar permite escolher se devem ser descartadas.

Os perfis ficam em `%LOCALAPPDATA%\OledDesktopMode\profiles.txt`, separadamente do registo de recuperação. A gravação é atómica e durável. Não existe pré-visualização que altere as cores durante a edição: enquanto a janela está visível, o monitor usa as cores normais; o novo perfil é aplicado na próxima transição para o Desktop.

A interface usa **WPF .NET 10 com o tema Fluent integrado**, não uma página web nem uma imitação desenhada dos controlos. `ThemeMode.System` acompanha o tema claro/escuro, a cor de destaque e os temas de alto contraste do sistema. O tema aplica o backdrop Mica onde suportado; os cantos arredondados são pedidos ao DWM. Layout em cartões, tipografia Segoe UI, scroll, navegação por teclado e nomes de acessibilidade acompanham os controlos reais WPF. O mecanismo `ThemeMode` ainda tem anotação experimental na API; a versão de compilação é .NET 10, com a supressão específica `WPF0001`.

A escolha foi informada também por discussões da comunidade, e confirmada no código/documentação do framework:

- [Reddit: WPF UI versus tema Fluent integrado](https://www.reddit.com/r/dotnet/comments/1huygnw/).
- [Reddit: WPF Fluent versus WinUI](https://www.reddit.com/r/csharp/comments/1gupt51/).
- [Reddit: mudança para WPF UI Fluent](https://www.reddit.com/r/csharp/comments/134mux0/).
- [Código oficial WPF: guia do tema Fluent e backdrop](https://github.com/dotnet/wpf/blob/main/Documentation/docs/using-fluent.md).

O processo da interface `OledSettings.exe` existe apenas enquanto a janela está aberta. Comunica com o motor por mensagens Win32 `WM_COPYDATA`, com limites de tamanho, validação no backend e timeout. Não existe um servidor HTTP nem um timer de sincronização. Fechar a interface não termina o motor/guardião. O backend só precisa dos componentes C++ anteriores; a publicação da UI inclui o runtime .NET para não obrigar a instalar .NET noutra máquina. Ao distribuir, manter `OledDesktopMode.exe` junto à pasta `ui` completa.

O efeito só é aplicado quando o Desktop é a superfície ativa **e não existe nenhuma janela de aplicação visível sobre qualquer monitor**. Abrir uma aplicação, uma pasta do Explorer, um menu do Desktop ou uma janela sobre o wallpaper restaura os valores normais. Clicar no wallpaper mantendo uma aplicação visível não ativa o efeito. Num sistema com várias telas, esta versão usa uma decisão global conservadora, mas guarda/aplica as cores separadamente por monitor.

```powershell
.\build\OledDesktopMode.exe --diagnose
.\build\OledDesktopMode.exe --inspect-desktop
```

`--diagnose` apenas lê: suporte e valores atuais das cinco propriedades. `--inspect-desktop` explica a decisão e lista as janelas visíveis que podem impedir o efeito.

Dois comandos de teste **alteram temporariamente as cores**:

```powershell
.\build\OledDesktopMode.exe --verify-hardware
.\build\OledDesktopMode.exe --verify-crash
```

O primeiro aplica/restaura o preset e testa cada propriedade isoladamente; o segundo aplica o preset e termina intencionalmente o processo com código 99, para o guardião restaurar as cores. Não executar estes testes simultaneamente com outra instância. O guardião usa o mesmo executável, pelo que aparecem dois processos durante o funcionamento normal.

## Investigação e escolha da API

| Opção | Resultado |
| --- | --- |
| AMD ADLX `IADLXDisplayCustomColor` | Escolhida: getters/setters, suporte e intervalos separados para os cinco parâmetros, objetos por monitor e notificações de alterações. |
| AMD ADL/ADL2 `Display_Color_Get/Set` | API oficial mais antiga, possível alternativa, mas exige mais gestão de adaptadores/índices e notificações. Sem fallback silencioso que possa mudar a semântica. |
| Win32 gamma ramp / ICC / DDC-CI | Controlam mecanismos distintos; não equivalem aos cinco parâmetros AMD Custom Color. |
| Automação da interface do Adrenalin | Desnecessária com ADLX disponível. |

Chamadas usadas, individualmente:

| Propriedade | Leitura | Escrita | OLED |
| --- | --- | --- | --- |
| Brightness | `GetBrightness` | `SetBrightness` | -17 |
| Contrast | `GetContrast` | `SetContrast` | 115 |
| Saturation | `GetSaturation` | `SetSaturation` | 122 |
| Hue | `GetHue` | `SetHue` | 0 |
| Color Temperature | `GetTemperature` | `SetTemperature` | 6400 K |

Cada método tem consulta `Is…Supported` e `Get…Range`. Propriedades não suportadas, com falha de leitura ou cujo intervalo não aceita o preset são ignoradas, sem arredondar os valores pedidos. Monitores sem identidade inequívoca ou sem qualquer propriedade elegível são ignorados. Apenas as propriedades elegíveis são gravadas/restauradas.

Estes setters são específicos da propriedade; o programa não chama setters de resolução, refresh rate, HDR, pixel format, gamma, gamut ou brilho físico DDC. No hardware disponível, o teste isolado confirmou que cada setter preservou as outras quatro propriedades Custom Color. A interface não fornece uma transação atómica para os cinco setters: existe uma sequência curta de chamadas, pelo que não se pode prometer ausência absoluta de artefactos em todos os drivers. Também não se afirma aqui que todos os interruptores auxiliares da interface do Adrenalin, como a origem EDID da temperatura, tenham sido validados: esta implementação lê e recupera as cinco propriedades documentadas pela interface Custom Color.

Fontes primárias consultadas:

- [AMD: Custom Color no Adrenalin](https://www.amd.com/en/resources/support-articles/faqs/DH3-021.html).
- [AMD GPUOpen: IADLXDisplayCustomColor](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/display/iadlxdisplaycustomcolor/).
- [AMD: sample oficial DisplayCustomColor, versão usada](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/32b5a740d42295c5dfe9026b9f52683da0f3af91/Samples/CPP/Display/DisplayCustomColor/mainDisplayCustomColor.cpp).
- [AMD: sample oficial de notificações externas](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/blob/32b5a740d42295c5dfe9026b9f52683da0f3af91/Samples/CPP/ReceivingEventsNotifications/SyncDisplayReceive/mainSyncDisplayReceive.cpp).
- [Microsoft: SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook).
- [Microsoft: EnumWindows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumwindows).
- [Microsoft: DwmGetWindowAttribute](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmgetwindowattribute).

## Arquitetura e segurança

`src/state.h` contém o controlador testável e `src/main.cpp` o backend ADLX, armazenamento, observador Win32, ícone e guardião.

1. Obter exclusão mútua da instância e ler qualquer registo de recuperação.
2. Restaurar primeiro qualquer backup pendente; se não for possível, bloquear a ativação. Nunca capturar valores OLED deixados por uma sessão interrompida.
3. Ler os valores reais atuais por monitor, sem defaults.
4. Iniciar o guardião e confirmar que está pronto antes de permitir OLED.
5. Na transição `NORMAL → OLED_DESKTOP`, reler o estado normal, gravar o backup com `pending=true`, fazer flush e substituição atómica do ficheiro, e só depois chamar a AMD.
6. Na transição inversa, restaurar apenas valores diferentes, verificar por leitura e só então limpar `pending`.

O ficheiro `%LOCALAPPDATA%\OledDesktopMode\recovery.txt` contém os backups; `events.log` regista transições/falhas. Não apagar o backup enquanto houver recuperação pendente. Um ficheiro inválido bloqueia novas alterações, em vez de substituir o backup por valores possivelmente OLED. A identificação combina GPU PNP, EDID e ID ADLX: não transfere configurações para outro monitor por posição na lista. Se o driver alterar a identidade após uma atualização/religação, a recuperação pode exigir intervenção; a aplicação recusa adivinhar a correspondência.

As notificações ADLX com origem externa atualizam `NormalState` apenas quando o monitor está em estado normal e sem recuperação pendente. Eventos próprios são ignorados; o preset completo nunca é adotado como novo normal durante a observação. Alterações feitas durante OLED são descartadas quando o normal é restaurado, conforme a regra de atualizar apenas em NORMAL.

O guardião espera no handle real do processo principal, sem polling. Quando esse processo termina, adquire o bloqueio, lê o backup e tenta restaurar os monitores presentes. Se o guardião terminar inesperadamente, o processo principal desativa o efeito e tenta restaurar. Falhas de restauração no processo principal têm tentativas espaçadas de 2 segundos; não há timer periódico em funcionamento saudável.

Fecho normal, desativação, bloqueio/desconexão de sessão, suspensão e encerramento do Windows têm tentativa de restauração. Cortes de energia, driver indisponível, monitor desligado ou término de **ambos** os processos impedem recuperação imediata. O backup permanece para recuperação no próximo arranque da aplicação. Não é instalada uma tarefa automática de arranque.

## Deteção do Desktop e performance

`SetWinEventHook` observa foreground, minimizar/restaurar, criar/destruir/mostrar/ocultar, mudança de posição e troca de desktop. A callback apenas agenda trabalho na thread da aplicação. A decisão exige:

- Desktop de entrada `Default`, acessível, sessão não bloqueada.
- Foreground pertencente ao Explorer do shell, com classe de Desktop/taskbar; uma janela de pasta do Explorer não passa este teste.
- `EnumWindows` não encontrar nenhuma janela visível, não minimizada e não cloaked pelo DWM que intersete a área dos monitores.

Classes internas do Explorer (`Progman`, `WorkerW`, taskbar) são identificadas também pelo PID do shell; estes nomes não constituem uma API pública de «wallpaper visível». A abordagem é conservadora e pode produzir falsos negativos com widgets e wallpapers interativos. Janelas auxiliares que combinam `WS_EX_LAYERED`, `WS_EX_TRANSPARENT` e `WS_EX_TOOLWINDOW` são tratadas como overlays passivos, desde que não sejam foreground. Janelas layered com alfa global comprovadamente zero também são ignoradas quando não são foreground. Não existe exceção pelo nome do processo: aplicações normais, incluindo ChatGPT e a janela de definições, continuam a impedir OLED. A semântica de passagem de input das janelas layered transparentes está documentada em [Microsoft: Window Features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features). Uma janela interativa visível não minimizável continua a impedir OLED mesmo após Win+D.

A saída de OLED é tratada logo na entrega do evento; a entrada é agrupada durante 35 ms para estabilizar a animação do Explorer. «Instantâneo» significa melhor esforço sujeito à entrega dos eventos e à latência do driver, sem garantia de tempo real. Não se reaplica o preset em eventos que mantêm o mesmo estado. Não se lançam processos durante as transições; o guardião é lançado uma vez por sessão da aplicação.

## Compilar

Requer CMake, compilador C++17 para Windows x64, SDK .NET 10 para a janela WPF e SDK oficial ADLX. A DLL ADLX é fornecida pelo driver AMD compatível, não deve ser descarregada de fontes externas.

```powershell
git clone https://github.com/GPUOpen-LibrariesAndSDKs/ADLX.git external/ADLX
git -C external/ADLX checkout 32b5a740d42295c5dfe9026b9f52683da0f3af91
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
dotnet publish ui/OledSettings.csproj -c Release -r win-x64 --self-contained true -o build/ui
```

Com Ninja e os SDKs instalados, `powershell -File build.ps1` compila o motor, publica a UI e executa os testes numa única etapa. `OledDesktopMode.exe --background` inicia apenas o motor. Para validar a interface com o monitor real, `build\ui\OledSettings.exe --verify-ui` exercita sliders, guardar, rejeição de valores inválidos e ativação, restaura o perfil inicial e encerra os processos de teste. O teste gera `build\ui\ui-verification.txt` e uma renderização do conteúdo WPF em `build\ui\ui-preview.png` (não inclui a barra de título/backdrop do DWM).

Com Visual Studio, omitir `-G Ninja` e usar `cmake --build build --config Release` e `ctest --test-dir build -C Release --output-on-failure`. O executável fica em `build\Release` nesse caso. O core e os testes também compilam fora do Windows; o backend real é exclusivo de Windows.

## Verificação realizada em 3 de outubro de 2026

- Compilação Windows x64 com Clang/LLVM-MinGW e CMake: passou; executável depende apenas de DLLs Windows/UCRT e ADLX carregada pelo driver.
- Teste de segurança da máquina de estados: passou (captura, transições, não reaplicação, alteração manual, exclusão do preset, suporte parcial, falha de persistência, rollback e recuperação de uma sessão interrompida).
- Extensão para perfis editáveis: testes passaram com dois monitores simulados, presets distintos e exclusão tanto do preset atual como do último preset aplicado da captura normal.
- UI Fluent real: compilação e teste de integração passaram; recursos do tema carregados, cinco controlos por monitor, slider sincronizado com entrada numérica, perfil guardado no backend e mantido após reiniciar o motor, valores inválidos rejeitados tanto na UI como no backend, ativação/desativação e preservação do perfil original/NormalState. As renderizações foram inspecionadas visualmente em tema claro e escuro; alto contraste e múltiplos DPI ainda precisam de validação interativa.
- AMD Radeon RX 6600, driver `32.0.21045.5002`, monitor `24B36X`: cinco propriedades suportadas; leitura original `0, 100, 100, 0, 6500`. Preset completo e restauração exata passaram.
- Setters isolados: preservaram as outras quatro propriedades e restauraram os valores originais.
- Término forçado do processo principal com preset aplicado: guardião restaurou o backup; registo de conclusão 47 ms após o término intencional.
- Funcionamento residente e fecho por mensagem Win32: sem falhas; amostra de repouso de 2 segundos registou 0 ms de CPU no processo principal (amostra breve, não benchmark).
- A primeira versão considerava uma janela auxiliar transparente do ChatGPT como bloqueio, impedindo a entrada automática no Desktop. Após corrigir a classificação de overlays passivos, o teste real de mostrar Desktop passou: foreground `Progman`, estado `OLED_DESKTOP`, leitura AMD exatamente igual ao perfil guardado pelo utilizador (`100, 200, 200, 30, 10000`). Ao voltar às aplicações, o motor entrou em `NORMAL` e a leitura voltou exatamente a `0, 100, 100, 0, 6500`. Bloqueio/suspensão/desligamento, mudanças manuais no Adrenalin e múltiplos monitores físicos ainda precisam de validação interativa nesses cenários. Os testes do controlador cobrem a lógica de atualização e independência por monitor.

As verificações terminaram com as cores originais restauradas e sem deixar a aplicação ativa em background.
