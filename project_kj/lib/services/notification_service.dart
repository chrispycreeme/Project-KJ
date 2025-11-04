import 'package:flutter/foundation.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';

class NotificationService {
  NotificationService();

  final FlutterLocalNotificationsPlugin _plugin =
      FlutterLocalNotificationsPlugin();
  bool _initialized = false;
  int _counter = 0;

  static const String _channelId = 'trap_events';

  Future<void> initialize() async {
    final androidSettings = const AndroidInitializationSettings(
      '@mipmap/ic_launcher',
    );

    final darwinSettings = const DarwinInitializationSettings(
      requestAlertPermission: true,
      requestBadgePermission: true,
      requestSoundPermission: true,
    );

    final initializationSettings = InitializationSettings(
      android: androidSettings,
      iOS: darwinSettings,
      macOS: darwinSettings,
    );

    await _plugin.initialize(initializationSettings);

    if (!kIsWeb && defaultTargetPlatform == TargetPlatform.android) {
      final androidPlugin = _plugin
          .resolvePlatformSpecificImplementation<
              AndroidFlutterLocalNotificationsPlugin>();

      const channel = AndroidNotificationChannel(
        _channelId,
        'Trap Events',
        description:
            'Notifications when the trap detects pests or triggers CO₂ release.',
        importance: Importance.high,
      );

      await androidPlugin?.createNotificationChannel(channel);
      await androidPlugin?.requestNotificationsPermission();
    }

    if (!kIsWeb && defaultTargetPlatform == TargetPlatform.iOS) {
      final iosPlugin = _plugin
          .resolvePlatformSpecificImplementation<
              IOSFlutterLocalNotificationsPlugin>();
      await iosPlugin?.requestPermissions(
        alert: true,
        badge: true,
        sound: true,
      );
    }

    if (!kIsWeb && defaultTargetPlatform == TargetPlatform.macOS) {
      final macPlugin = _plugin
          .resolvePlatformSpecificImplementation<
              MacOSFlutterLocalNotificationsPlugin>();
      await macPlugin?.requestPermissions(
        alert: true,
        badge: true,
        sound: true,
      );
    }

    _initialized = true;
  }

  Future<void> showRatDetected({
    required int delta,
    required int total,
  }) async {
    if (!_initialized) {
      return;
    }

    final title = delta == 1 ? 'Rat detected' : '$delta rats detected';
    final body = 'Total detections recorded: $total';

    await _showNotification(title: title, body: body);
  }

  Future<void> showCo2Release() async {
    if (!_initialized) {
      return;
    }

    await _showNotification(
      title: 'CO₂ release engaged',
      body: 'Trap has activated CO₂ release.',
    );
  }

  Future<void> _showNotification({
    required String title,
    required String body,
  }) async {
    final notificationId = _counter++;

    final androidDetails = AndroidNotificationDetails(
      _channelId,
      'Trap Events',
      channelDescription:
          'Notifications when the trap detects pests or triggers CO₂ release.',
      importance: Importance.high,
      priority: Priority.high,
      icon: '@mipmap/ic_launcher',
    );

    const darwinDetails = DarwinNotificationDetails();

    final details = NotificationDetails(
      android: androidDetails,
      iOS: darwinDetails,
      macOS: darwinDetails,
    );

    await _plugin.show(notificationId, title, body, details);
  }
}
