@echo off
echo Starting Proxy Tests...
echo =====================

echo 1. Testing Standard HTTP (Expect 200 OK)
curl.exe -v -x http://localhost:8888 http://example.com
echo.
echo ---------------------

echo 2. Testing HTTPS Tunnel (Expect 200 Connection Established)
curl.exe -v -x http://localhost:8888 https://www.google.com
echo.
echo ---------------------

echo 3. Testing Blocked Domain (Expect 403 Forbidden)
curl.exe -v -x http://localhost:8888 http://blocked.com
echo.
echo ---------------------

echo Tests Completed. Check logs/proxy.log for details.
pause