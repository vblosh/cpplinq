-- Ensure cppdb role and database are configured with superuser privileges
DO $$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_catalog.pg_roles WHERE rolname = 'cppdb') THEN
        CREATE ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password';
    ELSE
        ALTER ROLE cppdb WITH SUPERUSER LOGIN PASSWORD 'cppdb_password';
    END IF;
END $$;

SELECT 'CREATE DATABASE cppdb OWNER cppdb' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'cppdb')\gexec
GRANT ALL PRIVILEGES ON DATABASE cppdb TO cppdb;
