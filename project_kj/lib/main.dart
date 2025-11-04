import 'package:firebase_core/firebase_core.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import 'models/trap_data.dart';
import 'models/trap_log_entry.dart';
import 'services/notification_service.dart';
import 'services/trap_repository.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await _ensureFirebaseInitialized();

  final notificationService = NotificationService();
  await notificationService.initialize();

  final repository = TrapRepository();
  await repository.bootstrap();

  runApp(
    MainApp(
      repository: repository,
      notificationService: notificationService,
    ),
  );
}

Future<void> _ensureFirebaseInitialized() async {
  if (Firebase.apps.isNotEmpty) {
    return;
  }

  try {
    await Firebase.initializeApp();
  } on FirebaseException catch (error) {
    if (error.code != 'duplicate-app') {
      rethrow;
    }
  }
}

class MainApp extends StatelessWidget {
  const MainApp({
    required this.repository,
    required this.notificationService,
    super.key,
  });

  final TrapRepository repository;
  final NotificationService notificationService;

  @override
  Widget build(BuildContext context) {
    return MultiProvider(
      providers: [
        Provider<TrapRepository>.value(value: repository),
        Provider<NotificationService>.value(value: notificationService),
        ChangeNotifierProvider<AppThemeController>(
          create: (_) => AppThemeController(),
        ),
      ],
      child: Consumer<AppThemeController>(
        builder: (context, themeController, _) {
          return MaterialApp(
            title: 'Trap Control Center',
            themeMode:
                themeController.isDarkMode ? ThemeMode.dark : ThemeMode.light,
            theme: AppTheme.light,
            darkTheme: AppTheme.dark,
            home: const TrapDashboardScreen(),
            debugShowCheckedModeBanner: false,
          );
        },
      ),
    );
  }
}

class AppThemeController extends ChangeNotifier {
  bool _isDarkMode = false;

  bool get isDarkMode => _isDarkMode;

  void toggle() {
    _isDarkMode = !_isDarkMode;
    notifyListeners();
  }
}

class AppTheme {
  static ThemeData get light {
    final base = ThemeData.light(useMaterial3: true);
    return base.copyWith(
      colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xFF3E7EFF)),
      scaffoldBackgroundColor: Colors.white,
      textTheme: base.textTheme.apply(
        fontFamily: 'ProductSans',
        bodyColor: Colors.black87,
        displayColor: Colors.black87,
      ),
      cardTheme: base.cardTheme.copyWith(
        color: Colors.white,
        margin: const EdgeInsets.symmetric(vertical: 12),
        shadowColor: Colors.black12,
        elevation: 4,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(20),
        ),
      ),
      appBarTheme: base.appBarTheme.copyWith(
        elevation: 0,
        backgroundColor: Colors.white,
        foregroundColor: Colors.black87,
        titleTextStyle: base.textTheme.titleLarge?.copyWith(
          fontWeight: FontWeight.w600,
          fontSize: 20,
        ),
      ),
      switchTheme: base.switchTheme.copyWith(
        thumbColor: MaterialStateProperty.resolveWith(
          (states) => states.contains(MaterialState.selected)
              ? const Color(0xFF3E7EFF)
              : Colors.white,
        ),
        trackColor: MaterialStateProperty.resolveWith(
          (states) => states.contains(MaterialState.selected)
              ? const Color(0xFF3E7EFF).withOpacity(0.3)
              : Colors.black12,
        ),
      ),
    );
  }

  static ThemeData get dark {
    final base = ThemeData.dark(useMaterial3: true);
    return base.copyWith(
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xFF3E7EFF),
        brightness: Brightness.dark,
      ),
      scaffoldBackgroundColor: const Color(0xFF10121A),
      textTheme: base.textTheme.apply(
        fontFamily: 'ProductSans',
        bodyColor: Colors.white,
        displayColor: Colors.white,
      ),
      cardTheme: base.cardTheme.copyWith(
        color: const Color(0xFF181C26),
        margin: const EdgeInsets.symmetric(vertical: 12),
        shadowColor: Colors.black54,
        elevation: 4,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(20),
        ),
      ),
      appBarTheme: base.appBarTheme.copyWith(
        elevation: 0,
        backgroundColor: const Color(0xFF10121A),
        foregroundColor: Colors.white,
        titleTextStyle: base.textTheme.titleLarge?.copyWith(
          fontWeight: FontWeight.w600,
          fontSize: 20,
        ),
      ),
      switchTheme: base.switchTheme.copyWith(
        thumbColor: MaterialStateProperty.resolveWith(
          (states) => states.contains(MaterialState.selected)
              ? const Color(0xFF3E7EFF)
              : Colors.white,
        ),
        trackColor: MaterialStateProperty.resolveWith(
          (states) => states.contains(MaterialState.selected)
              ? const Color(0xFF3E7EFF).withOpacity(0.5)
              : Colors.white30,
        ),
      ),
    );
  }
}

class TrapDashboardScreen extends StatefulWidget {
  const TrapDashboardScreen({super.key});

  @override
  State<TrapDashboardScreen> createState() => _TrapDashboardScreenState();
}

class _TrapDashboardScreenState extends State<TrapDashboardScreen> {
  TrapData? _previousData;
  final List<_DashboardAlert> _alerts = <_DashboardAlert>[];
  int _alertSeed = 0;

  @override
  Widget build(BuildContext context) {
    final repository = context.read<TrapRepository>();
    final themeController = context.watch<AppThemeController>();

    return Scaffold(
      appBar: AppBar(
        title: const Text('Trap Control Center'),
        actions: [
          IconButton(
            tooltip: themeController.isDarkMode
                ? 'Switch to light mode'
                : 'Switch to dark mode',
            onPressed: themeController.toggle,
            icon: Icon(
              themeController.isDarkMode
                  ? Icons.light_mode_outlined
                  : Icons.dark_mode_outlined,
            ),
          ),
        ],
      ),
      body: StreamBuilder<TrapData>(
        stream: repository.watchTrapData(),
        builder: (context, snapshot) {
          if (snapshot.connectionState == ConnectionState.waiting) {
            return const Center(child: CircularProgressIndicator());
          }

          final data = snapshot.data ?? TrapData.defaults();
          _maybeNotifyEvents(data);

          return RefreshIndicator(
            onRefresh: () async {},
            child: SingleChildScrollView(
              physics: const AlwaysScrollableScrollPhysics(),
              padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  if (_alerts.isNotEmpty)
                    Column(
                      children: _alerts
                          .map(
                            (alert) => Padding(
                              padding: const EdgeInsets.only(bottom: 12),
                              child: _AlertBanner(
                                alert: alert,
                                onDismissed: () => _dismissAlert(alert.id),
                              ),
                            ),
                          )
                          .toList(growable: false),
                    ),
                  Text(
                    'Dashboard',
                    style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                          fontWeight: FontWeight.w600,
                        ),
                  ),
                  const SizedBox(height: 12),
                  _OverviewRow(data: data),
                  const SizedBox(height: 24),
                  Text(
                    'Controls',
                    style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                          fontWeight: FontWeight.w600,
                        ),
                  ),
                  const SizedBox(height: 12),
                  _ControlSwitchCard(
                    icon: Icons.pest_control,
                    title: 'Activate Trap',
                    subtitle: 'Manually arm or disarm the trap mechanism.',
                    value: data.activateTrap,
                    onChanged: (value) => _handleControlUpdate(
                      run: () =>
                          repository.updateControls(activateTrap: value),
                      label: 'Trap ${value ? 'activated' : 'deactivated'}',
                      payload: {'activate_trap': value},
                    ),
                  ),
                  _ControlSwitchCard(
                    icon: Icons.ramen_dining,
                    title: 'Feed Dispenser',
                    subtitle: 'Open or close the feed dispenser remotely.',
                    value: data.openFeedDispenser,
                    onChanged: (value) => _handleControlUpdate(
                      run: () => repository.updateControls(
                        openFeedDispenser: value,
                      ),
                      label: 'Feed dispenser ${value ? 'opened' : 'closed'}',
                      payload: {'open_feed_dispenser': value},
                    ),
                  ),
                  _ControlSwitchCard(
                    icon: Icons.cloud_outlined,
                    title: 'CO2 Release',
                    subtitle: 'Trigger the CO2 release manually.',
                    value: data.co2Release,
                    onChanged: (value) => _handleControlUpdate(
                      run: () => repository.updateControls(co2Release: value),
                      label: 'CO2 release ${value ? 'enabled' : 'disabled'}',
                      payload: {'co2_release': value},
                    ),
                  ),
                  NumberControlCard(
                    label: 'Cycles before CO2 release',
                    helperText:
                        'CO2 release triggers automatically after this many cycles.',
                    value: data.cyclesNeededForCo2Release,
                    onSubmit: (value) => _handleControlUpdate(
                      run: () => repository.updateControls(
                        cyclesNeededForCo2Release: value,
                      ),
                      label: 'CO2 release threshold updated',
                      payload: {
                        'cycles_needed_for_co2_release': value,
                      },
                    ),
                  ),
                  const SizedBox(height: 30),
                  Text(
                    'Action Log',
                    style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                          fontWeight: FontWeight.w600,
                        ),
                  ),
                  const SizedBox(height: 12),
                  _LogsList(repository: repository),
                ],
              ),
            ),
          );
        },
      ),
    );
  }

  Future<void> _handleControlUpdate({
    required Future<void> Function() run,
    required String label,
    Map<String, dynamic>? payload,
  }) async {
    final repository = context.read<TrapRepository>();

    try {
      await run();
      await repository.appendLog(label: label, payload: payload);
    } catch (error) {
      if (!mounted) {
        return;
      }
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Failed to update control: $error'),
        ),
      );
    }
  }

  void _maybeNotifyEvents(TrapData current) {
    final previous = _previousData;
    _previousData = current;

    if (previous == null) {
      return;
    }

    final events = <({
      String message,
      IconData icon,
      void Function(NotificationService service) notify,
    })>[];

    if (current.ratDetectedCount > previous.ratDetectedCount) {
      final diff = current.ratDetectedCount - previous.ratDetectedCount;
      final total = current.ratDetectedCount;
      final ratMessage = diff == 1
          ? 'Rat detected! Total detections: $total.'
          : '$diff rats detected! Total detections: $total.';
      events.add((
        message: ratMessage,
        icon: Icons.pest_control,
        notify: (service) =>
            service.showRatDetected(delta: diff, total: total),
      ));
    }

    if (!previous.co2Release && current.co2Release) {
      events.add((
        message: 'CO₂ release activated.',
        icon: Icons.cloud_outlined,
        notify: (service) => service.showCo2Release(),
      ));
    }

    if (events.isEmpty) {
      return;
    }

    for (final event in events) {
      _enqueueAlert(event.message, icon: event.icon);
    }

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) {
        return;
      }
      final service = context.read<NotificationService>();
      for (final event in events) {
        event.notify(service);
      }
    });
  }

  void _enqueueAlert(String message, {required IconData icon}) {
    final alert = _DashboardAlert(
      id: _alertSeed++,
      message: message,
      icon: icon,
      createdAt: DateTime.now(),
    );

    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) {
        return;
      }

      setState(() {
        _alerts.insert(0, alert);
        if (_alerts.length > 3) {
          _alerts.removeRange(3, _alerts.length);
        }
      });

      Future<void>.delayed(const Duration(seconds: 6)).then((_) {
        if (!mounted) {
          return;
        }
        final stillPresent = _alerts.any((element) => element.id == alert.id);
        if (stillPresent) {
          _dismissAlert(alert.id);
        }
      });
    });
  }

  void _dismissAlert(int id) {
    if (!mounted) {
      return;
    }

    setState(() {
      _alerts.removeWhere((alert) => alert.id == id);
    });
  }
}

class _DashboardAlert {
  const _DashboardAlert({
    required this.id,
    required this.message,
    required this.icon,
    required this.createdAt,
  });

  final int id;
  final String message;
  final IconData icon;
  final DateTime createdAt;
}

class _AlertBanner extends StatelessWidget {
  const _AlertBanner({required this.alert, required this.onDismissed});

  final _DashboardAlert alert;
  final VoidCallback onDismissed;

  @override
  Widget build(BuildContext context) {
    return Dismissible(
      key: ValueKey<int>(alert.id),
      direction: DismissDirection.endToStart,
      onDismissed: (_) => onDismissed(),
      background: Container(
        alignment: Alignment.centerRight,
        padding: const EdgeInsets.symmetric(horizontal: 16),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.errorContainer,
          borderRadius: BorderRadius.circular(16),
        ),
        child: Icon(
          Icons.close,
          color: Theme.of(context).colorScheme.onErrorContainer,
        ),
      ),
      child: Container(
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.primaryContainer,
          borderRadius: BorderRadius.circular(16),
        ),
        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
        child: Row(
          children: [
            Icon(
              alert.icon,
              color: Theme.of(context).colorScheme.onPrimaryContainer,
            ),
            const SizedBox(width: 16),
            Expanded(
              child: Text(
                alert.message,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      fontWeight: FontWeight.w600,
                      color: Theme.of(context)
                          .colorScheme
                          .onPrimaryContainer,
                    ),
              ),
            ),
            IconButton(
              tooltip: 'Dismiss',
              onPressed: onDismissed,
              icon: Icon(
                Icons.close,
                color: Theme.of(context).colorScheme.onPrimaryContainer,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _OverviewRow extends StatelessWidget {
  const _OverviewRow({required this.data});

  final TrapData data;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final availableWidth = constraints.maxWidth.isFinite
            ? constraints.maxWidth
            : MediaQuery.of(context).size.width;
        final useTwoColumns = availableWidth >= 560;
        const spacing = 16.0;
        final cardWidth = useTwoColumns
            ? (availableWidth - spacing) / 2
            : availableWidth;
        final firstCard = _DashboardCard(
          icon: Icons.sensors,
          label: 'Rats detected',
          value: data.ratDetectedCount.toString(),
          accentColor: Colors.orangeAccent,
        );
        final secondCard = _DashboardCard(
          icon: Icons.loop,
          label: 'Cycles completed',
          value: data.cyclesCompleted.toString(),
          accentColor: Colors.tealAccent,
        );

        return Wrap(
          spacing: spacing,
          runSpacing: spacing,
          children: [
            SizedBox(width: cardWidth, child: firstCard),
            SizedBox(width: cardWidth, child: secondCard),
          ],
        );
      },
    );
  }
}

class _DashboardCard extends StatelessWidget {
  const _DashboardCard({
    required this.icon,
    required this.label,
    required this.value,
    required this.accentColor,
  });

  final IconData icon;
  final String label;
  final String value;
  final Color accentColor;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: accentColor.withOpacity(0.2),
                borderRadius: BorderRadius.circular(16),
              ),
              child: Icon(icon, color: accentColor, size: 28),
            ),
            const SizedBox(height: 20),
            Text(
              value,
              style: Theme.of(context).textTheme.displaySmall?.copyWith(
                    fontWeight: FontWeight.w700,
                  ),
            ),
            const SizedBox(height: 6),
            Text(
              label,
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    color: Theme.of(context)
                        .textTheme
                        .bodyMedium
                        ?.color
                        ?.withOpacity(0.7),
                  ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ControlSwitchCard extends StatelessWidget {
  const _ControlSwitchCard({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.value,
    required this.onChanged,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
        child: Row(
          children: [
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Theme.of(context).colorScheme.primary.withOpacity(0.12),
                borderRadius: BorderRadius.circular(16),
              ),
              child: Icon(
                icon,
                color: Theme.of(context).colorScheme.primary,
                size: 28,
              ),
            ),
            const SizedBox(width: 20),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: Theme.of(context).textTheme.titleMedium?.copyWith(
                          fontWeight: FontWeight.w600,
                        ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    subtitle,
                    style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                          color: Theme.of(context)
                              .textTheme
                              .bodyMedium
                              ?.color
                              ?.withOpacity(0.7),
                        ),
                  ),
                ],
              ),
            ),
            Switch(value: value, onChanged: onChanged),
          ],
        ),
      ),
    );
  }
}

class NumberControlCard extends StatefulWidget {
  const NumberControlCard({
    required this.label,
    required this.helperText,
    required this.value,
    required this.onSubmit,
    super.key,
  });

  final String label;
  final String helperText;
  final int value;
  final Future<void> Function(int value) onSubmit;

  @override
  State<NumberControlCard> createState() => _NumberControlCardState();
}

class _NumberControlCardState extends State<NumberControlCard> {
  late final TextEditingController _controller;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _controller = TextEditingController(text: widget.value.toString());
  }

  @override
  void didUpdateWidget(covariant NumberControlCard oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.value != widget.value && !_isSaving) {
      _controller.text = widget.value.toString();
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              widget.label,
              style: Theme.of(context).textTheme.titleMedium?.copyWith(
                    fontWeight: FontWeight.w600,
                  ),
            ),
            const SizedBox(height: 6),
            Text(
              widget.helperText,
              style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                    color: Theme.of(context)
                        .textTheme
                        .bodyMedium
                        ?.color
                        ?.withOpacity(0.7),
                  ),
            ),
            const SizedBox(height: 16),
            Row(
              children: [
                _CircleIconButton(
                  icon: Icons.remove,
                  onPressed: _isSaving
                      ? null
                      : () => _handleIncrement(decrement: true),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: TextField(
                    controller: _controller,
                    keyboardType: TextInputType.number,
                    decoration: const InputDecoration(
                      border: OutlineInputBorder(),
                      isDense: true,
                    ),
                    onSubmitted: (_) => _submit(),
                  ),
                ),
                const SizedBox(width: 12),
                _CircleIconButton(
                  icon: Icons.add,
                  onPressed:
                      _isSaving ? null : () => _handleIncrement(decrement: false),
                ),
                const SizedBox(width: 12),
                FilledButton.icon(
                  onPressed: _isSaving ? null : _submit,
                  icon: _isSaving
                      ? const SizedBox(
                          width: 16,
                          height: 16,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Icon(Icons.save_outlined, size: 18),
                  label: const Text('Apply'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  void _handleIncrement({required bool decrement}) {
    final current = int.tryParse(_controller.text) ?? widget.value;
    final proposed = decrement ? current - 1 : current + 1;
    final safeValue = proposed < 1 ? 1 : proposed;
    _controller.text = safeValue.toString();
    _submit();
  }

  Future<void> _submit() async {
    final value = int.tryParse(_controller.text);
    if (value == null || value < 1) {
      _showError('Please enter a positive number.');
      _controller.text = widget.value.toString();
      return;
    }

    setState(() => _isSaving = true);

    try {
      await widget.onSubmit(value);
    } catch (error) {
      _showError('Failed to update value: $error');
    } finally {
      if (mounted) {
        setState(() => _isSaving = false);
      }
    }
  }

  void _showError(String message) {
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message)),
    );
  }
}

class _CircleIconButton extends StatelessWidget {
  const _CircleIconButton({required this.icon, required this.onPressed});

  final IconData icon;
  final VoidCallback? onPressed;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Theme.of(context).colorScheme.surfaceVariant,
      shape: const CircleBorder(),
      child: InkWell(
        customBorder: const CircleBorder(),
        onTap: onPressed,
        child: Padding(
          padding: const EdgeInsets.all(10),
          child: Icon(
            icon,
            size: 20,
            color: Theme.of(context).colorScheme.primary,
          ),
        ),
      ),
    );
  }
}

class _LogsList extends StatelessWidget {
  const _LogsList({required this.repository});

  final TrapRepository repository;

  @override
  Widget build(BuildContext context) {
    return StreamBuilder<List<TrapLogEntry>>(
      stream: repository.watchRecentLogs(limit: 30),
      builder: (context, snapshot) {
        if (snapshot.hasError) {
          return const _PlaceholderCard(
            icon: Icons.error_outline,
            message: 'Unable to load log entries.',
          );
        }

        if (!snapshot.hasData) {
          return const Center(child: CircularProgressIndicator());
        }

        final logs = snapshot.data!;
        if (logs.isEmpty) {
          return const _PlaceholderCard(
            icon: Icons.receipt_long,
            message: 'Actions will appear here once recorded.',
          );
        }

        return Card(
          child: ListView.separated(
            shrinkWrap: true,
            physics: const NeverScrollableScrollPhysics(),
            padding: const EdgeInsets.symmetric(vertical: 8),
            itemCount: logs.length,
            separatorBuilder: (_, __) => const Divider(
              indent: 72,
              endIndent: 20,
              height: 0,
            ),
            itemBuilder: (context, index) {
              final log = logs[index];
              final formattedTimestamp = DateFormat('MMM d � HH:mm').format(
                log.timestamp.toLocal(),
              );
              return ListTile(
                leading: Container(
                  padding: const EdgeInsets.all(10),
                  decoration: BoxDecoration(
                    color:
                        Theme.of(context).colorScheme.primary.withOpacity(0.12),
                    borderRadius: BorderRadius.circular(14),
                  ),
                  child: Icon(
                    Icons.track_changes,
                    color: Theme.of(context).colorScheme.primary,
                  ),
                ),
                title: Text(
                  log.label,
                  style: Theme.of(context).textTheme.titleMedium?.copyWith(
                        fontWeight: FontWeight.w600,
                      ),
                ),
                subtitle: Text(
                  formattedTimestamp,
                  style: Theme.of(context).textTheme.bodySmall?.copyWith(
                        color: Theme.of(context)
                            .textTheme
                            .bodySmall
                            ?.color
                            ?.withOpacity(0.7),
                      ),
                ),
                trailing: log.payload.isEmpty
                    ? null
                    : Tooltip(
                        message: log.payload.toString(),
                        child: const Icon(Icons.info_outline),
                      ),
              );
            },
          ),
        );
      },
    );
  }
}

class _PlaceholderCard extends StatelessWidget {
  const _PlaceholderCard({
    required this.icon,
    required this.message,
  });

  final IconData icon;
  final String message;

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 36),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(
              icon,
              size: 40,
              color: Theme.of(context).colorScheme.primary,
            ),
            const SizedBox(height: 12),
            Text(
              message,
              style: Theme.of(context).textTheme.bodyLarge,
              textAlign: TextAlign.center,
            ),
          ],
        ),
      ),
    );
  }
}
