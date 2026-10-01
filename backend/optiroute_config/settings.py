"""
Django settings for optiroute_config project.
"""

import os
from pathlib import Path

from dotenv import load_dotenv
from django.core.exceptions import ImproperlyConfigured

BASE_DIR = Path(__file__).resolve().parent.parent
PROJECT_ROOT = BASE_DIR.parent
FRONTEND_DIR = PROJECT_ROOT / 'frontend'

load_dotenv(PROJECT_ROOT / '.env')

PRODUCTION = os.getenv('APP_ENV') == 'production'
SECRET_KEY = os.getenv('SECRET_KEY', '' if PRODUCTION else 'django-insecure-dev-only-change-me')
if PRODUCTION and len(SECRET_KEY) < 50:
    raise ImproperlyConfigured('Production requires a random SECRET_KEY of at least 50 characters')
DEBUG = False if PRODUCTION else os.getenv('DEBUG', 'True').lower() == 'true'
ALLOWED_HOSTS = os.getenv('ALLOWED_HOSTS', 'localhost,127.0.0.1').split(',')

INSTALLED_APPS = [
    'django.contrib.staticfiles',
    'rest_framework',
    'api',
]

MIDDLEWARE = [
    'django.middleware.security.SecurityMiddleware',
    'whitenoise.middleware.WhiteNoiseMiddleware',
    'django.middleware.common.CommonMiddleware',
]

ROOT_URLCONF = 'optiroute_config.urls'

TEMPLATES = [
    {
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
        'DIRS': [FRONTEND_DIR],
        'APP_DIRS': False,
        'OPTIONS': {},
    },
]

WSGI_APPLICATION = 'optiroute_config.wsgi.application'

DATABASES = {
    'default': {
        'ENGINE': 'django.db.backends.dummy',
    }
}

REDIS_URL = os.getenv('REDIS_URL', 'redis://localhost:6379/0')
GOOGLE_MAPS_API_KEY = os.getenv('GOOGLE_MAPS_API_KEY', '')

CELERY_BROKER_URL = REDIS_URL
CELERY_ACCEPT_CONTENT = ['json']
CELERY_TASK_SERIALIZER = 'json'
CELERY_BROKER_CONNECTION_RETRY_ON_STARTUP = True
CELERY_WORKER_PREFETCH_MULTIPLIER = 1
CELERY_TASK_TIME_LIMIT = 45
CELERY_WORKER_MAX_TASKS_PER_CHILD = 100
# The engine is pure computation. Late acknowledgement permits redelivery after a lost connection.
CELERY_TASK_ACKS_LATE = True
CELERY_TASK_ACKS_ON_FAILURE_OR_TIMEOUT = True
TASK_RETENTION_SECONDS = int(os.getenv('TASK_RETENTION_SECONDS', '86400'))
TASK_MAX_AGE_SECONDS = int(os.getenv('TASK_MAX_AGE_SECONDS', '900'))
OPTIMIZATION_QUEUE_LIMIT = int(os.getenv('OPTIMIZATION_QUEUE_LIMIT', '20' if PRODUCTION else '0'))
OPTIMIZATION_MIN_INTERVAL_SECONDS = int(os.getenv('OPTIMIZATION_MIN_INTERVAL_SECONDS', '1' if PRODUCTION else '0'))
if TASK_RETENTION_SECONDS <= TASK_MAX_AGE_SECONDS or TASK_MAX_AGE_SECONDS <= CELERY_TASK_TIME_LIMIT:
    raise ImproperlyConfigured('Task retention must exceed queue age, which must exceed the execution limit')

REST_FRAMEWORK = {
    'DEFAULT_AUTHENTICATION_CLASSES': [],
    'EXCEPTION_HANDLER': 'api.exceptions.api_exception_handler',
    'DEFAULT_RENDERER_CLASSES': [
        'rest_framework.renderers.JSONRenderer',
    ],
    'UNAUTHENTICATED_USER': None,
}

LANGUAGE_CODE = 'en-us'
TIME_ZONE = 'UTC'
USE_I18N = True
USE_TZ = True

STATIC_URL = '/static/'
STATICFILES_DIRS = [FRONTEND_DIR]
STATIC_ROOT = BASE_DIR / 'staticfiles'
if PRODUCTION:
    STORAGES = {
        'staticfiles': {'BACKEND': 'whitenoise.storage.CompressedManifestStaticFilesStorage'},
    }
    # Only the private Docker network exposes the web process; Caddy supplies this header.
    SECURE_PROXY_SSL_HEADER = ('HTTP_X_FORWARDED_PROTO', 'https')
    SECURE_CONTENT_TYPE_NOSNIFF = True
    SECURE_REFERRER_POLICY = 'strict-origin-when-cross-origin'
DATA_UPLOAD_MAX_MEMORY_SIZE = 65536
