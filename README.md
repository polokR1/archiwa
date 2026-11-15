# Archiwa

Aplikacja na Windows 11 24H2 lub nowszy, x64. Instalacja jest przeznaczona dla bieżącego użytkownika. Program działa offline i korzysta z systemowego silnika archiwów Windows.

- **Zawartość archiwum** otwiera archiwum, pokazuje listę i pozwala sprawdzić dane. Zaznacz pliki lub foldery, aby rozpakować tylko je. Bez zaznaczenia program rozpakowuje całość.
- **Nowe archiwum** pakuje pliki i foldery do ZIP, 7z lub TAR.GZ. Dla ZIP można ustawić hasło AES-256.
- **Profile ZIP** zapisują folder źródłowy i docelowy, rozszerzenia plików, pomijane foldery i wzór nazwy. Przykład reguł obejmuje `pdf; jpg`, folder `Robocze` oraz wzór `{folder}_{date}`. Po kliknięciu **Pokaż pliki** program wyświetla ścieżki i proponowaną nazwę ZIP. Puste pole rozszerzeń obejmuje wszystkie pliki. Nazwy pomijanych folderów oddziela się średnikiem. Archiwum zachowuje podfoldery zawierające wybrane pliki.
- **Kolejka** rozpakowuje wiele archiwów do osobnych podfolderów i pokazuje wynik każdego pliku. Uszkodzone lub zaszyfrowane archiwum z wymaganym hasłem nie zatrzymuje pozostałych. Aby podać hasło do obsługiwanego formatu, użyj **Otwórz podgląd** i rozpakuj to archiwum osobno.
- **Eksplorator Windows** udostępnia po instalacji MSI polecenia **Rozpakuj tutaj** dla obsługiwanych rozszerzeń i **Spakuj według profilu** dla folderu. Znajdziesz je w **Pokaż więcej opcji**.
- **Ustawienia** zapisują język, kartę startową, domyślny format i poziom kompresji. Przy wybranym folderze rozpakowywania możesz pominąć każdorazowe pytanie o jego użycie i otwierać folder po zakończeniu. Przycisk **Przywróć domyślne** przygotowuje zmianę do zapisania.
- **Pomoc**, **Informacje** i **Kontakt** są dostępne po polsku i po angielsku. Pomoc działa offline. W Informacjach znajdziesz wersję programu. W Kontakcie możesz skopiować wersję i język do zgłoszenia oraz otworzyć skonfigurowany adres wsparcia.
- RAR jest dostępny do odczytu. Obsługa szyfrowania w tej wersji obejmuje ZIP. Obsługa archiwów wieloczęściowych zależy od formatu i systemowego silnika.
- Aplikacja działa bez instalowania po uruchomieniu `Archiwa.exe`. Lokalne wydania MSI i EXE są niepodpisane cyfrowo.

## Budowanie

Na Windows x64 z Visual Studio C++ Build Tools i Windows SDK uruchom w PowerShell `.\build.ps1`. Gotowe pliki pojawią się w katalogu nadrzędnym, obok folderu `Archiwa`. Po sklonowaniu repozytorium możesz użyć `.\build.ps1 -OutputDirectory .\dist`, aby zapisać wynik w klonie. Numer wydania ustala plik `version.txt`. Dołączony instalator zawiera aplikację w zasobach i korzysta z systemowej biblioteki Windows 11. Program nie wymaga osobnego środowiska .NET.

Opcja `.\build.ps1 -Package` wymaga dodatkowo Pythona i tworzy MSI oraz demonstracyjny MSIX. Instalator MSI zawiera wszystkie pliki aplikacji i obsługuje instalację cichą poleceniem `msiexec /i Archiwa-1.2.0-x64.msi /qn /norestart`. Jeśli przechodzisz z dotychczasowego instalatora EXE na MSI, odinstaluj wydanie EXE przed uruchomieniem MSI. Ustawienia wersji EXE są przechowywane w `%LOCALAPPDATA%\ArchiwaPreferences.dat`.

Parametry `-SupportEmail` i `-SupportUrl` ustawiają publiczny adres e-mail i stronę HTTPS w zakładce **Kontakt**. Wybór adresu e-mail otwiera szkic wiadomości w domyślnym kliencie poczty.

Skrypt `.\build.ps1 -Smoke` buduje też wersję instalatora do sprawdzenia kopiowania plików, skrótu, ponownej instalacji i usunięcia w tymczasowym folderze. Po takiej kompilacji uruchom test interfejsu poleceniem `python .\tests\verify-windows.py` z folderu `Archiwa`. Dla własnego katalogu wynikowego przekaż jego ścieżkę jako argument testu.

## Microsoft Store

MSIX z nazwą `Store-Preview` zawiera roboczą tożsamość techniczną. Po rezerwacji nazwy aplikacji skopiuj wartości **Package/Identity/Name**, **Package/Identity/Publisher** oraz **Package/Properties/PublisherDisplayName** ze strony tożsamości produktu w Partner Center.

```powershell
.\build.ps1 -Package -StoreIdentityName '<Identity Name>' -StorePublisher '<Identity Publisher>' -StorePublisherDisplayName '<Publisher Display Name>' -StoreDisplayName '<Zarezerwowana nazwa>'
```

Przed zgłoszeniem uruchom kontrolę Windows App Certification Kit na czystym Windows 11, sprawdź instalację i uruchomienie oraz przygotuj opis, zrzuty ekranu, adres polityki prywatności i informacje o cenie. Sprawdź również działanie systemowego `archiveint.dll` na wspieranych wydaniach Windows, ponieważ aplikacja korzysta z eksportów tego pliku. Microsoft podpisuje MSIX w ramach dystrybucji sklepowej po certyfikacji. W ścieżce MSI trzeba podpisać instalator i jego plik EXE oraz dostarczyć wersjonowany adres HTTPS.

[Wymagania pakietu MSIX](https://learn.microsoft.com/windows/apps/publish/store-readiness-checklist) · [Wymagania pakietu MSI](https://learn.microsoft.com/windows/apps/publish/publish-your-app/msi/app-package-requirements)
